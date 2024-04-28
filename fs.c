
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

// for the 8M file system
#define TFS_MAGIC  0xc3450545

#define NUM_BLOCKS 2048
#define NUM_INODES 512 
#define NUM_DENTRIES_PER_BLOCK 128

#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

#define BITS_PER_UINT 32

// file type
#define REGULAR 1
#define DIR 2

struct tfs_superblock {
    int signature ; // Signature 
    int num_blocks; // number of blocks; same as NUM_BLOCKS in this project
    int num_inodes; // number of inodes; same as NUM_INODES in this project
    int root_inode; // inode number of root directory ; use 1
    unsigned int block_in_use[NUM_BLOCKS/BITS_PER_UINT];
    unsigned int inode_in_use[NUM_INODES/BITS_PER_UINT];
};

struct tfs_dir_entry {
    int valid; 
    char fname[24];
    int inum;
};

struct tfs_inode {
    int type;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union tfs_block {
    struct tfs_superblock super;
    struct tfs_inode inode[INODES_PER_BLOCK];
    struct tfs_dir_entry dentry[NUM_DENTRIES_PER_BLOCK]; 
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

void tfs_debug(){ 
    int i;
    bool first;
    int root_direct;
    int num_blocks_in_use = 0;
    int num_inodes_in_use = 0;
    union tfs_block block;

    // Read the superblock
    printf("\nReading superblock...\n");
    union tfs_block superblock;
    disk_read(0,superblock.data);
    printf("      superblock:\n");
    // Check signature
    if(superblock.super.signature  == TFS_MAGIC)
        printf("            signature is valid\n");
    else
        printf("            signature is invalid\n");

    // Count blocks in use
    for(i=0; i<NUM_BLOCKS; i++){
        if(superblock.super.block_in_use[i / BITS_PER_UINT] & (1 <<(i % BITS_PER_UINT)))
            num_blocks_in_use++;
    }
    printf("            %d blocks in use \n", num_blocks_in_use); 

    // Count inodes in use 
    for (i = 0; i < NUM_INODES; i++) {
        if (superblock.super.inode_in_use[i / BITS_PER_UINT] & (1 << (i % BITS_PER_UINT))) {
            num_inodes_in_use++;
        }
    }
    printf("            %d inodes in use \n", num_inodes_in_use);

    printf("\nReading root directory...\n");
    // Read root inode entry
    disk_read(superblock.super.root_inode / INODES_PER_BLOCK + 1, block.data);
    struct tfs_inode root_inode = block.inode[superblock.super.root_inode % INODES_PER_BLOCK];
    printf("      root inode 1:\n");
    printf("            size: %d bytes\n", root_inode.size);
    for(i = 0; i<POINTERS_PER_INODE; i++){
        if(root_inode.direct[i]!=0){
            printf("            direct block: %d\n", root_inode.direct[i]);
            // Root directory only ever uses one direct block
            root_direct = root_inode.direct[i];
            break;
        }
    }

    printf("\nExploring root directory...\n");
    // Explore dir-entries in root directory
    if (root_inode.type == DIR && root_inode.size > 0) {
        disk_read(root_direct, block.data);
        for (i = 0; i < NUM_DENTRIES_PER_BLOCK; i++) {
            struct tfs_dir_entry dentry = block.dentry[i];
            if (dentry.valid) {
                // Print dir-entry
                printf("      %s inode %d:\n", dentry.fname, dentry.inum);
                // Read the inode entry
                union tfs_block curBlock;
                disk_read(dentry.inum / INODES_PER_BLOCK + 1, curBlock.data);
                // Print size
                printf("            size: %d bytes\n", curBlock.inode[dentry.inum % INODES_PER_BLOCK].size);
                // List direct blocks
                first = true;
                for(int j = 0; j<POINTERS_PER_INODE; j++){
                    if(curBlock.inode[dentry.inum % INODES_PER_BLOCK].direct[j]!=0){
                        if(first){
                            printf("            direct blocks: %d", curBlock.inode[dentry.inum % INODES_PER_BLOCK].direct[j]);
                            first = false;
                        }
                        else printf(", %d", curBlock.inode[dentry.inum % INODES_PER_BLOCK].direct[j]);
                    }
                }
                printf("\n");
                // Print indirect block
                if(curBlock.inode[dentry.inum % INODES_PER_BLOCK].indirect!=0){
                    printf("            indirect block: %d\n", curBlock.inode[dentry.inum % INODES_PER_BLOCK].indirect);
                    // Read indirect block
                    union tfs_block indirectBlock;
                    disk_read(curBlock.inode[dentry.inum % INODES_PER_BLOCK].indirect, indirectBlock.data);
                    // List all blocks that the indirect block points to
                    first = true;
                    for (int j = 0; j < POINTERS_PER_BLOCK; j++) {
                        if (indirectBlock.pointers[j] != 0) {
                            if (first){ 
                                printf("            indirect data blocks: %d", indirectBlock.pointers[j]);
                                first = false;
                            }
                            else printf(", %d", indirectBlock.pointers[j]);
                        }
                    }
                    printf("\n");
                }

            }
        }
    } else {
        printf("         root inode does not point to a valid directory structure\n");
    }

    printf("\nScanning inode table...\n");
    // Loop over each block in inode table
    for (int iblock = 1; iblock <= (NUM_INODES / INODES_PER_BLOCK); iblock++) {
        disk_read(iblock, block.data);
        // Loop over each inode entry in the block
        for (int i = 0; i < INODES_PER_BLOCK; i++) {
            int current_inode_number = (iblock - 1) * INODES_PER_BLOCK + i; 
            // Check if the inode is in use by looking at the superblock bitmap
            if (superblock.super.inode_in_use[current_inode_number / BITS_PER_UINT] & (1 << (current_inode_number % BITS_PER_UINT))) {
                if(current_inode_number == 0){
                    printf("      inode %d(reserved for null pointer):\n", current_inode_number);
                }
                else printf("      inode %d:\n", current_inode_number);
                printf("         size: %d bytes\n", block.inode[i].size);

                // List direct blocks
                printf("         direct blocks:");
                first = false;
                for (int j = 0; j < POINTERS_PER_INODE; j++) {
                    if (block.inode[i].direct[j] != 0) {
                        if(first){
                            printf(" %d", block.inode[i].direct[j]);
                            first = false;
                        }
                        else {
                            printf(", %d", block.inode[i].direct[j]);
                        }
                    }
                }
                printf("\n");

                // List indirect block
                if (block.inode[i].indirect != 0) {
                    printf("         indirect block: %d\n", block.inode[i].indirect);
                    printf("         indirect data blocks:");
                    union tfs_block indirectBlock;
                    disk_read(block.inode[i].indirect, indirectBlock.data);
                    first = true;
                    for (int j = 0; j < POINTERS_PER_BLOCK; j++) {
                        if (indirectBlock.pointers[j] != 0) {
                            if(first){
                                printf(" %d", indirectBlock.pointers[j]);
                                first = false;
                            }
                            else printf(", %d", indirectBlock.pointers[j]);
                        }
                    }
                    printf("\n");
                }
                printf("\n");
            }
        }
    }

}

int tfs_delete(const char *filename) {
    int inumber = tfs_get_inumber(filename);
    if (inumber <= 0) {
        return 0;
    }
    // Read the superblock
    union tfs_block superblock;
    disk_read(0, superblock.data);

    // Get the inode to be deleted
    int inode_block_number = inumber / INODES_PER_BLOCK + 1;
    union tfs_block inode_block;
    disk_read(inode_block_number, inode_block.data);
    struct tfs_inode *inode = &inode_block.inode[inumber % INODES_PER_BLOCK];

    // Free the direct blocks
    for (int i = 0; i < POINTERS_PER_INODE; i++) {
        if (inode->direct[i] != 0) {
            superblock.super.block_in_use[inode->direct[i] / BITS_PER_UINT] &= ~(1 << (inode->direct[i] % BITS_PER_UINT));
        }
    }

    // Free the indirect block
    if (inode->indirect != 0) {
        union tfs_block indirect_block;
        disk_read(inode->indirect, indirect_block.data);

        // Free the indirect data block
        for (int i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (indirect_block.pointers[i] != 0) {
                superblock.super.block_in_use[indirect_block.pointers[i] / BITS_PER_UINT] &= ~(1 << (indirect_block.pointers[i] % BITS_PER_UINT));
            }
        }

        // Free the indirect block itself
        superblock.super.block_in_use[inode->indirect / BITS_PER_UINT] &= ~(1 << (inode->indirect % BITS_PER_UINT));
    }

    // Clear the inode in the inode table
    memset(inode, 0, sizeof(struct tfs_inode));

    // Write back the updated inode block
    disk_write(inode_block_number, inode_block.data);

    // Update the inode bitmap in the superblock
    superblock.super.inode_in_use[inumber / BITS_PER_UINT] &= ~(1 << (inumber % BITS_PER_UINT));

    // Find the root directory block and inode
    int root_block_number = superblock.super.root_inode / INODES_PER_BLOCK + 1;
    union tfs_block root_block;
    disk_read(root_block_number, root_block.data);
    struct tfs_inode *root_inode = &root_block.inode[superblock.super.root_inode % INODES_PER_BLOCK];
    int root_dir_block_number = root_inode->direct[0];

    // Load the root directory's block
    union tfs_block dir_block;
    disk_read(root_dir_block_number, dir_block.data);

    // Find the directory entry for the file and clear it
    for (int i = 0; i < NUM_DENTRIES_PER_BLOCK; i++) {
        if (dir_block.dentry[i].valid && dir_block.dentry[i].inum == inumber) {
            memset(&dir_block.dentry[i], 0, sizeof(struct tfs_dir_entry));
            break;
        }
    }

    // Write back the updated directory block
    disk_write(root_dir_block_number, dir_block.data);

    // Write back the updated superblock
    disk_write(0, superblock.data);

    return inumber;
}


int tfs_get_inumber(const  char *filename ) {
    if (strlen(filename) >= sizeof(((struct tfs_dir_entry *)0)->fname)) {
        return 0;
    }

    // Read the superblock
    union tfs_block block;
    disk_read(0, block.data);

    // Read the root inode
    int root_inode_number = block.super.root_inode;
    int root_inode_block = root_inode_number / INODES_PER_BLOCK + 1;
    disk_read(root_inode_block, block.data);
    struct tfs_inode root_inode = block.inode[root_inode_number % INODES_PER_BLOCK];

    // Get the root inode's direct data block
    int root_direct;
    for(int i = 0; i<POINTERS_PER_INODE; i++){
        if(root_inode.direct[i]!=0){
            root_direct = root_inode.direct[i];
            break;
        }
    }
    disk_read(root_direct, block.data);

    // Find the file in the root's direct data block
    for (int i = 0; i < NUM_DENTRIES_PER_BLOCK; i++) {
        if (block.dentry[i].valid && strcmp(block.dentry[i].fname, filename) == 0) {
            return block.dentry[i].inum;
        }
    }

    return 0;
}

int tfs_getsize(const  char *filename ) {
    int inumber = tfs_get_inumber(filename);
    if (inumber <= 0) {
        return -1;
    }

    union tfs_block block;
    int inode_block = inumber / INODES_PER_BLOCK + 1;
    disk_read(inode_block, block.data);
    struct tfs_inode inode = block.inode[inumber % INODES_PER_BLOCK];

    return inode.size;
}

int tfs_read(int inumber, char *data, int length, int offset) {
    if (inumber <= 0 || length < 0 || offset < 0) {
        return 0;
    }

    union tfs_block block;
    union tfs_block inode_block;

    int inode_block_number = inumber / INODES_PER_BLOCK + 1;
    disk_read(inode_block_number, inode_block.data);
    struct tfs_inode inode = inode_block.inode[inumber % INODES_PER_BLOCK];

    // Offset passed end of file
    if (offset >= inode.size) {
        return 0;
    }

    // Truncate length if offset is too large
    if (offset + length > inode.size) {
        length = inode.size - offset;
    }

    // Read the data
    int bytes_read = 0;
    int block_number, block_offset;
    while (bytes_read < length) {
        int block_index = (offset + bytes_read) / DISK_BLOCK_SIZE;
        block_offset = (offset + bytes_read) % DISK_BLOCK_SIZE;

        // Determine whether the needed block is a direct block or an indirect block
        if (block_index < POINTERS_PER_INODE) {
            block_number = inode.direct[block_index];
        } else {
            union tfs_block indirect_block;
            disk_read(inode.indirect, indirect_block.data);
            block_number = indirect_block.pointers[block_index - POINTERS_PER_INODE];
        }
        if (block_number <= 0) {
            break;
        }

        // Read the data from the block
        disk_read(block_number, block.data);
        int to_read = DISK_BLOCK_SIZE - block_offset;
        if (bytes_read + to_read > length) {
            to_read = length - bytes_read;
        }
        memcpy(data + bytes_read, block.data + block_offset, to_read);
        bytes_read += to_read;
    }

    return bytes_read;
}


