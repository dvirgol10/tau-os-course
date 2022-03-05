#include "os.h"

/* the number of levels in this multi-level trie-based page table is 5 because:
the page/frame size is 4KB, a page table node occupy a physical page frame, the size of a page table entry is 64 bits (64bit addresses) ->
-> each node has 4KB/8B = 512 PTEs/children ->
-> 512=2^9, so accessing a PTE requires 9 bits, and
only the lower 57 bits of a virtual address are used for translation, and the lower 12 bits are used for specify the page offset ->
-> 57-12=45 bits are used for the page table walk. Therefore, there are 45/9=5 levels in the page table. */
#define NLEVELS 5
/* we have to use a mask of 9 bits in order retrieve an offset in a page table node. Note that 0x1ff = 1 1111 1111b*/
#define OFFSET_MASK 0x1ff

void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
	uint64_t* current_node = phys_to_virt(pt << 12); /* the starting address of the page table root is the first address in the page `pt` */
	unsigned int offset_in_node;
	uint64_t next_node;
	uint64_t* nodes_addresses[NLEVELS]; /* an array of addresses of page table nodes to easily freeing unnecessary nodes*/
	nodes_addresses[0] = current_node;

	for (int i = 0; i < NLEVELS - 1; i++) { /* page table walk */
		/* use the appropriate 9 bits of the vpn to retrieve the offset where the address of
		the next page table node is located */
		offset_in_node = (vpn >> (9 * (NLEVELS - i - 1))) & OFFSET_MASK;
		next_node = current_node[offset_in_node];

		if (!(next_node & 0x1)) { /* the first bit in a page table entry is the valid bit */
			/* if the goal of the update is to unmap `vpn` and there is no mapping for this address anyway,
			we don't have to do anything*/
			if (ppn == NO_MAPPING) {
				return;
			} else { /* otherwise, we have to allocate the appropriate node in the page table*/
				next_node = alloc_page_frame() << 12;
				current_node[offset_in_node] = next_node + 1;
			}
		} else {
			next_node -= 1; /* subtract the unwanted valid bit */
		}

		current_node = phys_to_virt(next_node);
		nodes_addresses[i + 1] = current_node;
	}

	offset_in_node = vpn & OFFSET_MASK; /* offset of the final page table entry of `vpn`*/


	unsigned int offset_in_parent_node; /* holds the offset of the entry (in the parent node) that points to the page table node in question*/
	if (ppn == NO_MAPPING) { /* destroy the `vpn`'s mapping (if it exists) and free page table nodes if needed */
		if (!(current_node[offset_in_node] & 0x1)) { /* if there is already no mapping for `vpn`, we don't have to free any page table node */
			return;
		}
		/* update the entry of the mapping for `vpn` to be `NO_MAPPING`,
		and leave its valid bit to be 0*/
		current_node[offset_in_node] = NO_MAPPING << 12;

		for (int i = NLEVELS - 1; i > 0; i--) { /* backward page table walk to free unnecessary page table nodes */
			for (int j = 0; j < 512; j++) { /* there are 512 page table entries in each node */
				if (nodes_addresses[i][j] & 0x1) { /* if there is some mapping in this node, there is no need to free it */
					return;
				}
			}

			/* if there is no mapping in this node,
			we need to free it and update its parent node*/
			offset_in_parent_node = (vpn >> (9 * (NLEVELS - i))) & OFFSET_MASK;
			free_page_frame(nodes_addresses[i - 1][offset_in_parent_node] >> 12);
			/* update the entry of the freed node to be `NO_MAPPING`,
			and leave its valid bit to be 0*/
			nodes_addresses[i - 1][offset_in_parent_node] = NO_MAPPING << 12;
		}
	} else {
		current_node[offset_in_node] = (ppn << 12) + 1; /* complete the mapping of `vpn` to `ppn`*/
	}
	return;
}

uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
	uint64_t* current_node = phys_to_virt(pt << 12); /* the starting address of the page table root is the first address in the page `pt` */
	unsigned int offset_in_node;
	uint64_t next_node;

	for (int i = 0; i < NLEVELS - 1; i++) { /* page table walk */
		/* use the appropriate 9 bits of the vpn to retrieve the offset where the address of
		the next page table node is located */
		offset_in_node = (vpn >> ( 9 * (NLEVELS - i - 1))) & OFFSET_MASK;
		next_node = current_node[offset_in_node];

		if (!(next_node & 0x1)) { /* the first bit in a page table entry is the valid bit */
			return NO_MAPPING; /* no mapping exists for `vpn`*/
		} else {
			next_node -= 1; /* subtract the unwanted valid bit */
		}

		current_node = phys_to_virt(next_node);
	}

	offset_in_node = vpn & OFFSET_MASK; /* offset of the final page table entry of `vpn`*/

	/* return the ppn that `vpn` is mapped to, or `NO_MAPPING` if no mapping exists */
	if (!(current_node[offset_in_node] & 0x1)) { /* the first bit in a page table entry is the valid bit */
		return NO_MAPPING;
	}
	return current_node[offset_in_node] >> 12; 
}