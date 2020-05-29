//Zachary A Taylor

/**
*@file disk-defrag.c
*@author Zachary Taylor
*This program takes a single parameter representing a fragmented
*disk image and returns a defragmented version of it
*/
 

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

/**The number of members that are read from/written to a file in
 * calls to fread and fwrite
 */
#define RW_NMEMB 1
/**The size of a superblock, as defined in the problem statement */
#define SUPERBLOCK_SIZE 512
/**The size of the boot block, as defined in the problem statement */
#define BOOT_BLOCK_SIZE 512
/**The size of an inode in bytes, as defined in the problem statement */
#define INODE_SIZE 100
/**Macro that indicates a given inode is unused */
#define UNUSED_INODE_SENTINEL -1
/**Macro used to zero out a free data block in regions beyond the initial
 *four bytes used as a pointer
 */
#define FREE_BLOCK_ZERO 0
/** The maximum number of direct pointers an inode can have */
#define N_DBLOCKS 10
/** The maximum number of single-indirect pointers an inode can have */
#define N_IBLOCKS 4
/** Macro used in calls to defrag to indicate that no recursion is needed */
#define ZERO_LEVELS 0
/** Macro used in calls to defrag to indicate that one level of recursion is needed */
#define ONE_LEVEL 1
/** Macro used in calls to defrag to indicate that two levels of recursion is needed */
#define TWO_LEVELS 2
/** Macro used in calls to defrag to indicate that three levels recursion is needed */
#define THREE_LEVELS 3

/**
 * Defines an inode in the inode region of a disk.
 * An unused inode's next_inode field will point to 
 * the next free inode, whereas an inode being used
 * will not have this value be used at all
 */
typedef struct
{
    int next_inode;         /* list for free inodes */
    int protect;            /*  protection field */
    int nlink;              /* Number of links to this file */
    int size;               /* Number of bytes in file */
    int uid;                /* Owner's user ID */
    int gid;                /* Owner's group ID */
    int ctime;              /* Time field */
    int mtime;              /* Time field */
    int atime;              /* Time field */
    int dblocks[N_DBLOCKS]; /* Pointers to data blocks */
    int iblocks[N_IBLOCKS]; /* Pointers to indirect blocks */
    int i2block;            /* Pointer to doubly indirect block */
    int i3block;            /* Pointer to triply indirect block */
} inode;

/**
 * Defines a superblock that is part of disk image
 * A superblock is 512 bytes in size
 */
typedef struct
{
    int blocksize;    /* size of blocks in bytes */
    int inode_offset; /* offset of inode region in blocks */
    int data_offset;  /* data region offset in blocks */
    int swap_offset;  /* swap region offset in blocks */
    int free_inode;   /* head of free inode list */
    int free_block;   /* head of free block list */
} superblock;

//----------------------
// Global: error_msg
//----------------------

/**
 * Function that prints an error message and exits the program
 * @param msg the message to print
 */
void error_msg(char *msg)
{
    printf("%s\n", msg);
    exit(EXIT_FAILURE);
}

//-----------------------
// Global: getValidInodes
//-----------------------

/**
 * Function that determines which inodes are being used on disk, and which are not
 * @param inodeOffset offset into the inode region
 * @param dataOffset offset into the data region
 * @param inodeSize the size of an inode
 * @param blockSize the size of a block
 * @param buffer pointer to memory region representing the disk itself
 * @return a pointer to a block of integers in memory whose values at each index refer to 
 * inode start addresses in the buffer
 */
int *getValidInodes(int inodeOffset, int dataOffset, int inodeSize, int blockSize, char *buffer)
{
    //total number of valid inodes
    int numValidInodes = 0;
    //the total possible number of inodes in the region
    int totalInodes = ((dataOffset - inodeOffset) * blockSize) / inodeSize;
    //will store the locations of valid inodes
    int *inodeLocations = malloc(totalInodes * sizeof(int));
    //for loop iteration variable
    int m;
    //get number of valid inodes
    for (m = 0; m < totalInodes; m++)
    {
        //address/index into buffer
        //get starting address of the inodeRegion
        int inodeStart = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (inodeOffset * blockSize);

        //get specific address of beginning of an inode
        int inodeAddr = inodeStart + (m * inodeSize);
        //cast thing at this location to an inode pointer
        inode *i = (inode *)(&(buffer[inodeAddr]));
        //indicates an inode that's in use
        if (i->nlink > 0)
        {
            numValidInodes++;
            inodeLocations[m] = inodeAddr;
        }
        else
        {
            inodeLocations[m] = UNUSED_INODE_SENTINEL;
        }
    }
    //allocate enough space for the number of valid inodes' starting indices in the buffer (these'll be integers)
    //also, allocate one space at the end of the malloc'd region for storing a sentinel value
    int *validInodeLocations = (int *)malloc(sizeof(int) * (numValidInodes + 1));

    //malloc fail condition
    if (validInodeLocations == NULL)
    {
        error_msg("Allocating memory for inode locations failed.\n");
    }

    //iterate through inodeLocations array, taking valid entries and putting their values in validInodeLocations
    //block of allocated memory

    //iteration variable for validInodeLocations
    int k = 0;
    for (m = 0; m < totalInodes; m++)
    {
        if (inodeLocations[m] != UNUSED_INODE_SENTINEL)
        {
            validInodeLocations[k] = inodeLocations[m];
            k++;
        }
    }
    //fill last location of validInodeLocations with sentinel value
    validInodeLocations[k] = UNUSED_INODE_SENTINEL;

    //free unused variables
    free(inodeLocations);

    //return the pointer to the caller
    return validInodeLocations;
}

//------------------------
// Global: zeroFreeBlock
//------------------------

/**
 * Function that zeroes out all but the first four bytes
 * of a free data block
 * @param freeBlockAddr starting address of the free block (i.e. pointer to first 4 bytes)
 * @param blocksize the size of a block on disk
 * @param buffer the buffer to examine representing disk image
 */
void zeroFreeBlock(int freeBlockAddr, int blocksize, char *buffer)
{
    //iteration variable
    int i = 1;
    //number of four-bytes segments to clear
    int numSegments = blocksize / sizeof(int);
    for (i = 1; i < numSegments; i++)
    {
        int *currAddr = (int *)(&buffer[freeBlockAddr + (i * sizeof(int))]);
        *currAddr = FREE_BLOCK_ZERO;
    }
}

//------------------------
// Global: defrag
//------------------------

/**
 * Function that recursively defragments a given disk image by updating
 * the data blocks an inode points to.
 * @param buffer pointer to the old buffer
 * @param newBuffer pointer to the new buffer to modify
 * @param levels the number of levels of recursion to perform
 * @param blocksize the size of a data block
 * @param dataRegStartOffset the start of the data region in the original buffer (in blocks)
 * @param dataRegCurrOffset the current offset into the data region (in blocks)
 * @param inodeLocation the address in the inode region that currInode is at
 * @param nextFreeGroup integer pointer holding value of next free group of data blocks
 * @return an integer value indicating an offset (in blocks) into the data region; this offset
 * is useful for the original caller of the function, but not so for recursive calls; instead,
 * dataRegCurrOffset will be used in the recursive situation and incremented as blocks are filled in.
 * When recusion reaches its base case, the value returned then will be the one the original caller
 * of the function receives.
 */
int defrag(char *buffer, char *newBuffer, int levels, int blocksize, int dataRegStartOffset, int dataRegCurrOffset, int inodeLocation, int *nextFreeGroup)
{
    //cast the thing at inodeLocation in buffer to a proper inode
    inode currInode = *(inode *)(&buffer[inodeLocation]);

    //figure out address of next group of free data blocks
    *nextFreeGroup = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * dataRegCurrOffset);

    //recusion base case
    if (levels == 0)
    {
        //iteration variable
        int i = 0;
        for (i = 0; i < N_DBLOCKS; i++)
        {
            if (currInode.dblocks[i] != UNUSED_INODE_SENTINEL)
            {
                //get data block number in the original buffer to copy
                int blockIdx = currInode.dblocks[i];
                //use blockIdx to get the address of that data block in original buffer
                int blockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * blockIdx);
                //copy the data there to the next available block in the newBuffer
                memcpy(&newBuffer[*nextFreeGroup], &buffer[blockAddr], blocksize);
                //change inode content in original buffer to reflect new direct data block pointer(s)
                inode *k = (inode *)(&buffer[inodeLocation]);
                k->dblocks[i] = dataRegCurrOffset;
                //increment currentOffset into the data region to indicate a block was used and is no longer free
                dataRegCurrOffset++;
                //increment nextFreeGroup variable to give memory address of next free block in new buffer
                *nextFreeGroup += blocksize;
            }
        }

        //copy modified inode content to desired memory location in newBuffer's inode region
        memcpy(&newBuffer[inodeLocation], &buffer[inodeLocation], INODE_SIZE);
    }
    else if (levels == 1)
    {
        //make recursive call beforehand to make sure DBLOCKS are placed first
        dataRegCurrOffset = defrag(buffer, newBuffer, levels - 1, blocksize, dataRegStartOffset, dataRegCurrOffset, inodeLocation, nextFreeGroup);
        //iteration variable
        int i = 0;
        for (i = 0; i < N_IBLOCKS; i++)
        {
            if (currInode.iblocks[i] != UNUSED_INODE_SENTINEL)
            {
                //copy the block representing iblock into position in front of the data blocks it points to in the new image
                //get data block number in the original buffer to copy
                int blockIdx = currInode.iblocks[i];
                //use blockIdx to get the address of that indirect block in original buffer
                int blockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * blockIdx);
                //copy this to the next free location in newBuffer's data blocks
                memcpy(&newBuffer[*nextFreeGroup], &buffer[blockAddr], blocksize);
                //retain address of this iblock for use later
                int iblockAddr = *nextFreeGroup;
                //change inode content in original buffer to reflect new indirect data block pointer(s)
                inode *k = (inode *)(&buffer[inodeLocation]);
                k->iblocks[i] = dataRegCurrOffset;

                //increment currentOffset into the data region to indicate a block was used and is no longer free
                dataRegCurrOffset++;
                //update nextFreeGroup to have address of next free block
                *nextFreeGroup += blocksize;

                //iterate through this indirect block, figuring out which four byte segments have integer
                //values corresponding to data blocks

                //maximum number of direct blocks that can be pointed to by one indirect block
                int maxNumDirectBlocks = blocksize / sizeof(int);
                //iteration variable
                int j = 0;
                for (j = 0; j < maxNumDirectBlocks; j++)
                {
                    //dbIdx == direct block index
                    //dereferences explained: take the address of this iblock in the original buffer,
                    //then add some multiple of a 4-byte (i.e. integer-sized) offset to it based on the
                    //current iteration. Then, take the four bytes at a given offset and convert it to
                    //an integer value and examine that value
                    int dbIdx = *(int *)(&buffer[blockAddr + (j * sizeof(int))]);
                    //copy the data block indicated by the given offset if valid
                    if (dbIdx != UNUSED_INODE_SENTINEL)
                    {
                        //address of given data block
                        int dbAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * dbIdx);
                        memcpy(&newBuffer[*nextFreeGroup], &buffer[dbAddr], blocksize);
                        //update indirect block at the particular index with the value of the data block offset it has now
                        int *newBufferIblock = (int *)(&newBuffer[iblockAddr + sizeof(int) * j]);
                        *newBufferIblock = dataRegCurrOffset;
                        //increment currentOffset into newBuffer data region to indicate a block was used and is no longer free
                        dataRegCurrOffset++;
                        //update nextFreeGroup to have address of next free block in newBuffer
                        *nextFreeGroup += blocksize;
                    }
                }
            }
        }
        //copy modified inode content to desired memory location in newBuffer's inode region
        memcpy(&newBuffer[inodeLocation], &buffer[inodeLocation], INODE_SIZE);
    }
    else if (levels == 2)
    {
        //make recursive call to place DBLOCKS and IBLOCKS before doing anything else
        dataRegCurrOffset = defrag(buffer, newBuffer, levels - 1, blocksize, dataRegStartOffset, dataRegCurrOffset, inodeLocation, nextFreeGroup);
        //if i2block is being used
        if (currInode.i2block != UNUSED_INODE_SENTINEL)
        {
            //get location of i2 block as an offset into data region of original buffer
            int blockIdx = currInode.i2block;
            //use blockIdx to get the address of that data block in original buffer
            int blockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * blockIdx);
            //copy the i2block to the next free block in newBuffer
            memcpy(&newBuffer[*nextFreeGroup], &buffer[blockAddr], blocksize);

            //change inode content in original buffer to reflect new i2block pointer values
            inode *k = (inode *)(&buffer[inodeLocation]);
            k->i2block = dataRegCurrOffset;

            //save address of start of i2block for easy updating in newBuffer loops later on
            int i2StartAddr = *nextFreeGroup;

            //increment nextFreeGroup and dataRegCurrOffset as usual
            *nextFreeGroup += blocksize;
            dataRegCurrOffset++;

            //compute maximum possible number of references to indirect blocks in an i2block
            int maxIblockRefs = blocksize / sizeof(int);
            //iteration variable
            int i = 0;
            //go through i2 block, taking valid pointers to i blocks and then data blocks, etc.
            for (i = 0; i < maxIblockRefs; i++)
            {
                //indirect block idx
                //start of i2block address plus some 4-byte multiple offset
                int iblockIdx = *(int *)(&buffer[blockAddr + (sizeof(int) * i)]);
                if (iblockIdx != UNUSED_INODE_SENTINEL)
                {
                    //get iblock starting address in buffer based on value of iblockIdx
                    int iblockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * iblockIdx);

                    //copy iblock into newBuffer before copying its data blocks
                    memcpy(&newBuffer[*nextFreeGroup], &buffer[iblockAddr], blocksize);

                    //get start address of this particular iblock in newBuffer so you can update it later
                    int iblockStartAddr = *nextFreeGroup;

                    //update i2block pointer value in newBuffer to show new location of this iblock
                    int *update = (int *)(&newBuffer[i2StartAddr + (sizeof(int) * i)]);
                    *update = dataRegCurrOffset;

                    //update nextFreeGroup and dataRegCurrOffset
                    *nextFreeGroup += blocksize;
                    dataRegCurrOffset++;

                    //maximum number of data block references per indirect block
                    int maxDbPtrs = maxIblockRefs;
                    //iteration variable
                    int j = 0;
                    //go through iblock, getting valid direct block pointers
                    for (j = 0; j < maxDbPtrs; j++)
                    {
                        //index into a particular location in this iblock in original buffer
                        int dbIdx = *(int *)(&buffer[iblockAddr + sizeof(int) * j]);
                        //copy data block at given location if it's valid
                        if (dbIdx != UNUSED_INODE_SENTINEL)
                        {
                            //address of the data block that dbIdx is referring to
                            int dbAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * dbIdx);

                            //copy the data block at this address to the next free location in newBuffer
                            memcpy(&newBuffer[*nextFreeGroup], &buffer[dbAddr], blocksize);

                            //update this inode's contents in newBuffer
                            int *update = (int *)(&newBuffer[iblockStartAddr + sizeof(int) * j]);
                            *update = dataRegCurrOffset;

                            //update nextFreeGroup and dataRegCurrOffset
                            *nextFreeGroup += blocksize;
                            dataRegCurrOffset++;
                        }
                    }
                }
            }
        }
        //copy modified inode content to desired memory location in newBuffer's inode region
        memcpy(&newBuffer[inodeLocation], &buffer[inodeLocation], INODE_SIZE);
    }
    else
    {
        //make recursive call with levels == one less than previous value
        dataRegCurrOffset = defrag(buffer, newBuffer, levels - 1, blocksize, dataRegStartOffset, dataRegCurrOffset, inodeLocation, nextFreeGroup);
        //if i3block is being used
        if (currInode.i3block != UNUSED_INODE_SENTINEL)
        {
            //get location of i3 block as an offset into data region of original buffer
            int blockIdx = currInode.i3block;
            //use blockIdx to get the address of that data block in original buffer
            int blockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * blockIdx);
            //copy the i3block to the next free block in newBuffer
            memcpy(&newBuffer[*nextFreeGroup], &buffer[blockAddr], blocksize);
            //change inode content in original buffer to reflect new i3 pointer value
            inode *k = (inode *)(&buffer[inodeLocation]);
            k->i3block = dataRegCurrOffset;

            //save address of start of i3block for easy updating in newBuffer loops later on
            int i3blockStartAddr = *nextFreeGroup;

            //increment nextFreeGroup and dataRegCurrOffset as usual
            *nextFreeGroup += blocksize;
            dataRegCurrOffset++;

            //compute maximum possible number of references to doubly indirect blocks in an i3block
            int maxI2blockRefs = blocksize / sizeof(int);
            //iteration variable
            int m = 0;
            //go through i3 block, taking valid pointers to i2 blocks, then iblocks, then data blocks, etc.
            for (m = 0; m < maxI2blockRefs; m++)
            {
                //doubly indirect block index
                //i3block start address plus some 4-byte multiple offset
                int i2blockIdx = *(int *)(&buffer[blockAddr + (sizeof(int) * m)]);
                if (i2blockIdx != UNUSED_INODE_SENTINEL)
                {
                    //get i2block starting address in buffer based on value of i2blockIdx
                    int i2blockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * i2blockIdx);

                    //copy i2block into newBuffer before copying its indirect blocks
                    memcpy(&newBuffer[*nextFreeGroup], &buffer[i2blockAddr], blocksize);

                    //get start address of this particular i2block in newBuffer so you can update it later
                    int i2blockStartAddr = *nextFreeGroup;

                    //update i3block pointer value in newBuffer to show new location of this i2block
                    int *update = (int *)(&newBuffer[i3blockStartAddr + sizeof(int) * m]);
                    *update = dataRegCurrOffset;

                    //update nextFreeGroup and dataRegCurrOffset
                    *nextFreeGroup += blocksize;
                    dataRegCurrOffset++;

                    //maximum number of indirect references per doubly indirect block
                    int maxIbPtrs = maxI2blockRefs;
                    //iteration variable
                    int i = 0;
                    for (i = 0; i < maxIbPtrs; i++)
                    {
                        //indirect block index
                        //i2block starting address plus some 4-byte multiple offset
                        int iblockIdx = *(int *)(&buffer[i2blockAddr + sizeof(int) * i]);
                        if (iblockIdx != UNUSED_INODE_SENTINEL)
                        {
                            //get iblock starting address in buffer based on iblockIdx
                            int iblockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * iblockIdx);

                            //copy iblock into newBuffer before copying its direct blocks
                            memcpy(&newBuffer[*nextFreeGroup], &buffer[iblockAddr], blocksize);

                            //get starting address of this particular iblock in newBuffer so you can update it later
                            int iblockStartAddr = *nextFreeGroup;

                            //update i2block pointer value in newBuffer to show new location of this iblock
                            int *i2Update = (int *)(&newBuffer[i2blockStartAddr + sizeof(int) * i]);
                            *i2Update = dataRegCurrOffset;

                            //update nextFreeGroup and dataRegCurrOffset
                            *nextFreeGroup += blocksize;
                            dataRegCurrOffset++;

                            //maximum number of direct references per indirect block
                            int maxDbPtrs = maxIbPtrs;
                            //iteration variable
                            int v = 0;
                            for (v = 0; v < maxDbPtrs; v++)
                            {
                                //direct block index
                                //indirect block starting address pluse some 4-byte multiple offset
                                int dBlockIdx = *(int *)(&buffer[iblockAddr + sizeof(int) * v]);
                                //if index is valid, copy the given data block
                                if (dBlockIdx != UNUSED_INODE_SENTINEL)
                                {
                                    //get data block startind address in buffer based on dBlockIdx
                                    int dblockAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * dataRegStartOffset) + (blocksize * dBlockIdx);

                                    //copy data block into newBuffer before copying its direct block
                                    memcpy(&newBuffer[*nextFreeGroup], &buffer[dblockAddr], blocksize);
                                    //update iblock pointer value in newBuffer to show new location of this dblock
                                    int *iUpdate = (int *)(&newBuffer[iblockStartAddr + sizeof(int) * v]);
                                    *iUpdate = dataRegCurrOffset;

                                    //update nextFreeGroup and dataRegCurrOffset
                                    *nextFreeGroup += blocksize;
                                    dataRegCurrOffset++;
                                }
                            }
                        }
                    }
                }
            }
        }
        //copy modified inode content to desired memory location in newBuffer's inode region
        memcpy(&newBuffer[inodeLocation], &buffer[inodeLocation], INODE_SIZE);
    }

    return dataRegCurrOffset;
}

//-----------------------
// Global: main
//-----------------------

/**
 * Main entry point of the program
 * @param argc the number of arguments given
 * @param argv array of pointers to first character of 
 * each space-separated command-line argument
 * @return 0 on successful exit of the program and a different value
 * upon non-successful exit.
 */
int main(int argc, char *argv[])
{
    //check that number of arguments is valid
    if (argc != 2)
    {
        error_msg("Invalid number of command line arguments!");
    }

    //Steps: read in disk image, then get size of disk image using stat() system call

    // stat struct that will hold information about file
    struct stat fileInfo;
    
    int rc = stat(argv[1], &fileInfo);
    //error-handling for file having invalid stat() return
    if (rc != 0)
    {
        error_msg("Error determing disk image size.");
    }

    //  allocate enough space for the disk image
    //file pointer
    FILE *f;
    //open file for reading
    f = fopen(argv[1], "r");
    //error for invalid file pointer
    if (f == NULL)
    {
        error_msg("Error reading disk image file.");
    }
    //number of disk-sized members read from disk image
    size_t numMembers;
    //allocate char * buffer of size of the disk image file
    char *buffer = malloc(fileInfo.st_size);
    //read in the disk image file - fread returns the number of disk image-sized things it read in from the file
    numMembers = fread(buffer, fileInfo.st_size, RW_NMEMB, f);
    if (numMembers != 1)
    {
        error_msg("Error reading disk image file");
    }

    // read in the superblock and relevant data
    superblock *sb = (superblock *)&(buffer[SUPERBLOCK_SIZE]);
    //size of blocks on disk
    int blocksize = sb->blocksize;
    //offset for inode region
    int inodeOffset = sb->inode_offset;
    //offset for data region
    int dataOffset = sb->data_offset;
    //offset for swap region
    int swapOffset = sb->swap_offset;
    //head of free inode list
    int inodeListHead = sb->free_inode;
    //head of free block list
    int blockListHead = sb->free_block;

    //inode region start address
    int inodeRegionStart = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (inodeOffset * blocksize);
    //data region start address
    int dataRegionStart = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (dataOffset * blocksize);
    //swap region start address
    int swapRegionStart = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (swapOffset * blocksize);

    //allocate a new buffer representing the new disk image
    char *newBuffer = malloc(fileInfo.st_size);

    //copy entire original buffer
    memcpy(&newBuffer[0], &buffer[0], fileInfo.st_size);

    //pointer returned that indicates locations (buffer indices) of the start location of valid inodes
    int *validInodeLocations = getValidInodes(inodeOffset, dataOffset, INODE_SIZE, blocksize, buffer);

    //iteration variable
    int i = 0;
    //calculate number of valid inodes
    while (validInodeLocations[i] != UNUSED_INODE_SENTINEL)
    {
        i++;
    }

    //number of valid inodes
    int numInodes = i;

    //pointer holding address of next free group of data blocks
    int *nextFreeGroup = malloc(sizeof(int));
    *nextFreeGroup = dataRegionStart;
    //current offset into data region (in blocks) of the new buffer representing the new disk image
    int dataRegCurrOffset = 0;
    //for each valid inode, examine the size of files and start process of defragmenting disk
    for (i = 0; i < numInodes; i++)
    {
        //validInodeLocations[i] is location (buffer index) of start of i_th valid inode
        //cast data at memory location of buffer[inodeIdx] to an inode ptr, then dereference
        //to get a proper inode
        inode currInode = *(inode *)(&buffer[validInodeLocations[i]]);

        //see how many levels of recursion are required in defrag call
        if (currInode.i3block != UNUSED_INODE_SENTINEL)
        {
            dataRegCurrOffset = defrag(buffer, newBuffer, THREE_LEVELS, blocksize, dataOffset, dataRegCurrOffset, validInodeLocations[i], nextFreeGroup);
        }
        else if (currInode.i2block != UNUSED_INODE_SENTINEL)
        {
            dataRegCurrOffset = defrag(buffer, newBuffer, TWO_LEVELS, blocksize, dataOffset, dataRegCurrOffset, validInodeLocations[i], nextFreeGroup);
        }
        else if (currInode.iblocks[0] != UNUSED_INODE_SENTINEL)
        {
            dataRegCurrOffset = defrag(buffer, newBuffer, ONE_LEVEL, blocksize, dataOffset, dataRegCurrOffset, validInodeLocations[i], nextFreeGroup);
        }
        else if (currInode.dblocks[0] != UNUSED_INODE_SENTINEL)
        {
            dataRegCurrOffset = defrag(buffer, newBuffer, ZERO_LEVELS, blocksize, dataOffset, dataRegCurrOffset, validInodeLocations[i], nextFreeGroup);
        }
    }

    //Create a new free block list in the now defragmented disk

    //the starting place/offset (in terms of blocks) of free block list from beginning of disk
    int freeBlockListOffset = dataRegCurrOffset + dataOffset;
    //takes place of dataRegCurrOffset in loop to allow for easy iteration
    int currDrgOffset = dataRegCurrOffset;
    //make a new free list starting from freeBlockListOffset
    //calculate number of blocks between the current block and the
    int numberOfFreeBlocks = swapOffset - freeBlockListOffset;
    //base address of the free block list
    int freeBlockBaseAddr = (BOOT_BLOCK_SIZE + SUPERBLOCK_SIZE) + (blocksize * freeBlockListOffset);
    //current address in free block list
    int freeBlockCurrAddr = freeBlockBaseAddr;
    //iteration variable
    i = 0;
    for (i = 0; i < numberOfFreeBlocks; i++)
    {
        //get address of current free block
        freeBlockCurrAddr = freeBlockBaseAddr + (blocksize * i);
        //make a pointer to first four bytes of this address
        int *freeBlockPtr = (int *)(&newBuffer[freeBlockCurrAddr]);
        //next offset (relative to data block) that this block will point to
        currDrgOffset++;
        int nextOffset = currDrgOffset;
        *freeBlockPtr = nextOffset;
        //zero out the free block
        zeroFreeBlock(freeBlockCurrAddr, blocksize, newBuffer);
    }
    //set thing at last address to -1 to show free list is at its end
    int *freeBlockPtr = (int *)(&newBuffer[freeBlockCurrAddr]);
    *freeBlockPtr = UNUSED_INODE_SENTINEL;

    //update newBuffer's superblock to indicate that offset of free list has changed
    superblock *nSB = (superblock *)(&newBuffer[SUPERBLOCK_SIZE]);
    nSB->free_block = freeBlockListOffset - dataOffset;

    //write new buffer out to a file named disk_defrag_k, where k is
    //the number of the original disk image file -- use fwrite for this
    FILE *newFile;
    char * diskImageFile = argv[1];
    char * diskImageFileNumPtr = &diskImageFile[strlen(diskImageFile) - 1];
    char filename_part[FILENAME_MAX] = "output-disk-image/disk-defrag-";
    char * filename = strcat(filename_part, diskImageFileNumPtr);
    newFile = fopen(filename, "w");
    fwrite(&newBuffer[0], fileInfo.st_size, RW_NMEMB, newFile);

    //free resources
    free(validInodeLocations);
    free(buffer);
    free(newBuffer);
    free(nextFreeGroup);

    return 0;
}