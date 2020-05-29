# Disk Defrag Help
You can copy the boot and swap areas
Then, you need to read in the superblock
Important Superblock Fields:
	- Block size: size of superblock
	- Inode offset: tells you how to calculate address of inode region with following formula:
		- address of starting address of inode is 1K + block_size + inode_offset
		- inodes aren't block aligned, but don't worry about this too much
	- free_inode: points to head of the free inode list; this is an index, not a physical address. This
		index is an index into the inode region. An invalid inode will be at location/index -1. The
		inodes are connected by "links" essentially, with invalid inode terminating with -1. Don't
		modify this. Use the same inode so that the inode number won't change, but instead, only the
		addresses within the inode will change.
	- free_block: points to the first free data block. Have to cast first four bytes of data block to an
		integer, and the result will point to the next free data block. Unlike inode free list, you
		need to make sure that you sort the free data block list to allow for contiguous allocation
		among the data blocks.
	
	
	