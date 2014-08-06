/*
 * The author disclaims copyright to this source code.  In place of
* a legal notice, here is a blessing:
*
*    May you do good and not evil.
*    May you find forgiveness for yourself and forgive others.
*    May you share freely, never taking more than you give.
 */


/*
 * MemoryAbstraction.h
 *
 *  Created on: May 20, 2014
 *      Author: brian
 */

#ifndef MEMORYABSTRACTION_H_
#define MEMORYABSTRACTION_H_
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <algorithm>
#include <string.h>
#include <math.h>
#include <vector>
namespace MemoryAbstraction {
class Stream {
public:
	virtual int Read(uint64_t position, void* buffer, int count) = 0;
	virtual void Write(uint64_t position, const void* buffer, int count) = 0;
	virtual uint64_t GetLength() = 0;
	template<typename T>
	int Read(uint64_t position, T& buffer) {
		return Read(position,&buffer,sizeof(buffer));
	}
	template<typename T>
	void Write(uint64_t position, const T& buffer) {
		Write(position,&buffer,sizeof(buffer));
	}
	virtual ~Stream(){};
};
template<typename T>
class Reference {
public:
	T rval;
	Stream* str;
	uint64_t offset;
	Reference(Stream* str, uint64_t offset){
		this->str = str;
		this->offset = offset;
	}
	operator T() {
		T retval;
		str->Read(offset,retval);
		return retval;
	}
	const T val() {
		return (T)(*this);
	}
	Reference& operator=(const T& other) {
		str->Write(offset,other);
		return *this;
	}
	operator bool() {
		return offset;
	}
	Reference() {
str = 0;
offset = 0;
	}

};
class MemoryMappedFileStream:public Stream {
public:
	int fd;
	uint64_t sz;
	unsigned char* ptr;
	uint64_t GetLength() {
		return sz;
	}
	MemoryMappedFileStream(int fd, uint64_t sz) {
		this->fd = fd;
		this->sz = -1;
		ptr = (unsigned char*)mmap(0,sz,PROT_READ | PROT_WRITE, MAP_SHARED, fd,0);
		if((size_t)ptr == -1) {
			ptr = (unsigned char*)mmap(0,sz,PROT_READ | PROT_WRITE, MAP_SHARED, fd,0);
		}
        ptr = 0;
	}
	int Read(uint64_t position, void* buffer, int count) {
		if(position>sz) {
			return 0;
		}
		int available = std::min((int)(sz-position),count);
		if(available<0) {
			available = count;
		}
		memcpy(buffer,ptr+position,available);
		return available;
	}
	void Write(uint64_t position, const void* buffer, int count) {
		memcpy(ptr+position,buffer,count);
	}
	~MemoryMappedFileStream(){
		munmap(ptr,sz);
		close(fd);
	}
};
typedef struct {
	uint64_t next;
	uint64_t prev;
	uint64_t size;
} MemoryBlock;
class MemoryAllocator {
public:
	Stream* str;
	//Number of chunk sizes
	int numberOfChunks;
	uint64_t ReadChunk(int idx) {
		uint64_t retval;
		str->Read(idx*8,retval);
		return retval;
	}
	void WriteChunk(int idx, uint64_t val) {
		str->Write(idx*8,val);
	}
	void SetRootPtr(uint64_t root) {
			WriteChunk(numberOfChunks,root);
		}
	template<typename T>
	void SetRootPtr(const Reference<T>& root) {
		WriteChunk(numberOfChunks,root.offset);
	}
	uint64_t AllocateBytes(uint64_t size) {
        return (uint64_t)calloc(1,size);
        
		if(size<sizeof(MemoryBlock)) {
			size+=sizeof(MemoryBlock);
		}
		int chunkID = (int) std::ceil(log2(size));
		bool found = false;
		uint64_t pointer;
		for (; chunkID < numberOfChunks; chunkID++) {
			if ((pointer = ReadChunk(chunkID))) {
				found = true;
				break;
			}
		}
		if (!found) {
			//Not enough space -- insufficient funds to afford a bigger drive
			throw "Insufficient funds";
		}
		//Found a chunk -- remove it from the list of free blocks
		MemoryBlock chunk;
		str->Read(pointer,chunk);
		if(chunk.next) {
			MemoryBlock other;
			str->Read(chunk.next,other);
			other.prev = 0;
			str->Write(chunk.next,other);
		}
		WriteChunk(chunkID,chunk.next);
		//Check if we have additional free space after this block
		if(chunk.size-size>sizeof(MemoryBlock)) {

			uint64_t leftover = chunk.size-size-sizeof(MemoryBlock);
			//Found free space! Register it.
			RegisterFreeBlock(pointer+size,leftover);
		}
		return pointer;
	}
	void RegisterFreeBlock(uint64_t position, uint64_t sz) {
		int chunkID = (int)log2(sz);
		uint64_t pointer = ReadChunk(chunkID);
		MemoryBlock block;
		block.next = pointer;
		block.prev = 0;
		block.size = sz;
		if(pointer) {
			//Update linked list
			MemoryBlock other;
			str->Read(pointer,other);
			other.prev = position;
			str->Write(pointer,other);
		}
		str->Write(position,block);
		//Update root pointer
		WriteChunk(chunkID,position);
	}
	MemoryAllocator(Stream* str, uint64_t& rootPtr, uint64_t mappedLen) {
        
		this->str = str;
		return;
        uint64_t slen = mappedLen;
		numberOfChunks = log2(slen)+1;
		rootPtr = ReadChunk(numberOfChunks);
		if(!rootPtr) {
			//Compute space and write free block
			uint64_t freespace = slen-((numberOfChunks+2)*8)-sizeof(MemoryBlock)-32;
			RegisterFreeBlock((numberOfChunks+2)*8,freespace);
		}
	}

	void Free(uint64_t ptr, uint64_t sz) {
        //TODO: PROBLEM WITH FREE
        free((void*)ptr);
        return;
        printf("CORRUPTION CAUSED HERE (or is it just something writing outside its memory space?)\n");
       
		if(sz<sizeof(MemoryBlock)) {
					sz+=sizeof(MemoryBlock);
				}
				RegisterFreeBlock(ptr,sz);

	}
	uint64_t Allocate(uint64_t sz) {
		return AllocateBytes(sz);
	}
	template<typename T>
	Reference<T> Allocate() {
		T val;
		Reference<T> rval = Reference<T>(str,Allocate(sizeof(T)));
		rval = val;
		return rval;
	}
	template<typename T>
	void Free(const Reference<T>& ptr) {
		int64_t sz = sizeof(T);
		Free(ptr.offset,sz);
	}
};

template<typename T>
static size_t BinarySearch(const T* A,size_t arrlen, const T& key, int& insertionMarker) {
	if(arrlen == 0) {
		insertionMarker = 0;
		return -1;
	}
	int max = arrlen-1;
	int min = 0;
	while(max>=min) {
		insertionMarker = min+((max-min)/2);
		if(A[insertionMarker] == key) {
			return insertionMarker;
		}
		if(A[insertionMarker]<key) {
			min = insertionMarker+1;
		}else {
			max = insertionMarker-1;
		}
	}
	return -1;
}
template<typename T>
static size_t BinaryInsert(T* A, int32_t& len, const T& key) {
	if(len == 0) {
		A[0] = key;
		len++;
		return 0;
	}
	int location;
	BinarySearch(A,len,key,location);
	if(key<A[location]) {
		//Insert to left of location
		//Move everything at location to the right
		memmove(A+location+1,A+location,(len-location)*sizeof(T));
		//Insert value
		A[location] = key;
	}else {
		//Insert to right of location (this is ALWAYS called at end of list, so memmove is not necessary)
		location++;
		//Move everything at location to the right
		memmove(A+location+1,A+location,(len-location)*sizeof(T));
		A[location] = key;
	}
	len++;
	return location;
}


//Represents a B-tree
template<typename T, uint64_t KeyCount = 1024>
class BTree {
public:
	class Key {
		public:
			T val;
			uint64_t left;
			uint64_t right;
			Key() {
				left = 0;
				right = 0;
			}
			Key(const T& val) {
				this->val = val;
				left = 0;
				right = 0;
			}
			Key(const T& val, uint64_t left, uint64_t right) {
				this->val = val;
				this->left = left;
				this->right = right;
			}
		};
	class Node {
		public:
			//The length of the keys array
			int length;
			//Keys within this node (plus one auxillary node used for the sole
			//purpose of splitting)
			T keys[KeyCount+1];
			uint64_t children[KeyCount+2];
			uint64_t parent;
			Node() {
				parent = 0;
				memset(keys,0,sizeof(keys));
				memset(children,0,sizeof(children));
				length = 0;
			}
			void nodemove(size_t destoffset, size_t srcoffset, size_t len) {
				if(len == 0) {
					return;
				}
				//Zero-fill memmove
				memmove(keys+destoffset,keys+srcoffset,len);
				//memset(keys+srcoffset,0,len*sizeof(keys[0]));
				memmove(children+destoffset,children+srcoffset,(len)*sizeof(children[0]));
				//memset(children+srcoffset,0,(len-1)*sizeof(children[0]));
				printf("TODO: TEST THIS\n");
			}
			size_t Insert(const Key& val) {

				size_t rval = BinaryInsert(keys,length,val.val);
				memmove(children+rval+1,children+rval,((length-rval))*sizeof(children[0]));
				children[rval] = val.left;
				children[rval+1] = val.right;
				return rval;
			}
		};
	//Node is a leaf if it has no children
		bool IsLeaf(const Node& val) {
			for(size_t i = 0;i<KeyCount+1;i++) {
				if(getLeft(val,i)) {
					return false;
				}
			}
			return true;
		}
	Reference<Node> root;
	MemoryAllocator* allocator;
	BTree(MemoryAllocator* allocator, Reference<Node> root) {
		this->allocator = allocator;
		this->root = root;
	}
	BTree(MemoryAllocator* allocator, uint64_t rootPtr) {
		this->allocator = allocator;
		if(rootPtr !=0) {
		this->root = Reference<Node>(allocator->str,rootPtr);
		}else {
			//Allocate our root node
			this->root = allocator->Allocate<Node>();
		}
	}
	BTree() {
		this->allocator = 0;
	}
	bool Find(T& value, Reference<Node>& nodeptr, int& keyIndex) {
		nodeptr = root;
		Node current = root;
		int& marker = keyIndex;
		int offset = 0;
		while(true) {
			T mkey = value;
			if(BinarySearch(current.keys+offset,current.length-offset,mkey,marker) !=-1) {
				value = current.keys[marker+offset];
				marker = marker+offset;
				return true;
			}
			//Are there sub-trees?
			if(IsLeaf(current)) {
				marker = marker+offset;
				return false;
			}
			//Find the appropriate sub-tree and traverse it
			if(value<current.keys[marker+offset]) {
				//Take the left sub-tree
				//nodeptr = Reference<Node>(allocator->str,current.keys[marker+offset].left);
				nodeptr = D(getLeft(current,marker+offset));
				current = nodeptr;
			}else {
				//Take the right sub-tree
				//nodeptr = Reference<Node>(allocator->str,current.keys[marker+offset].right);
				nodeptr = D(getRight(current,marker+offset));
				current = nodeptr;
			}
		}
		marker = marker+offset;
		return false;
	}
	//Finds the first element matching the specified key
	bool Find(T& value) {
		Reference<Node> nodeptr;
		int keyIndex;
		return Find(value,nodeptr,keyIndex);
	}
	void fixupParents(Reference<Node> leftPtr, Reference<Node> rightPtr, Node& left, Node& right) {
		for (size_t i = 0; i<left.length; i++) {
			if (getLeft(left,i)) {
				Reference<Node> nptr = D(getLeft(left,i));
				Node n = nptr;
				n.parent = leftPtr.offset;
				nptr = n;
				nptr = D(getRight(left,i));
				n = nptr;
				n.parent = leftPtr.offset;
				nptr = n;
			}
			if (D(getLeft(right,i))) {
				//Reference<Node> nptr = Reference<Node>(allocator->str, right.keys[i].left);
				Reference<Node> nptr = D(getLeft(right,i));
				Node n = nptr;
				n.parent = rightPtr.offset;
				nptr = n;
				nptr = D(getRight(right,i));
				n = nptr;
				n.parent = rightPtr.offset;
				nptr = n;
			}

		}
	}
	void nodecopy(Node& dest, Node& src, size_t destoffset, size_t srcoffset, size_t len) {
		memcpy(dest.keys+destoffset,src.keys+srcoffset,len*sizeof(src.keys[0]));
		memcpy(dest.children+destoffset,src.children+srcoffset,(len+1)*sizeof(src.children[0]));

	}
	void findGlobalMax(Reference<Node>& nodePtr, Node& node) {
		if(node.children[node.length-1]) {
			nodePtr = D(node.children[node.length]);
			node = nodePtr;
			findGlobalMax(nodePtr,node);
		}

	}
	void Insert(Key value, Reference<Node> root, bool treatAsLeaf = false) {
		//Find a leaf node
		Reference<Node> current = root;
		while(!IsLeaf(current)) {
			if(treatAsLeaf) {
				break;
			}
			//Scan for the insertion point
			Node node = current;
			int marker;
			BinarySearch(node.keys,node.length,value.val,marker);
			if(value.val<node.keys[marker]) {
				//Proceed along the left sub-tree
				current = D(getLeft(node,marker));
			}else {
				//Proceed along the right sub-tree
				current = D(getRight(node,marker));
			}
		}
		Node node = current;
		//We've found a leaf!
		if(node.length<KeyCount) {
			//We can just do the insertion
			node.Insert(value);
			//Update the file
			current = node;
			return;
		}
		//Full node -- needs split; don't keep on disk anymore
		allocator->Free(current);
		if(node.parent !=0) {
			Reference<Node> p_ref = Reference<Node>(allocator->str,node.parent);
			Node p = p_ref;
			int marker;
			BinarySearch(p.keys,p.length,node.keys[0],marker);
			if(node.keys[0]<p.keys[marker]) {
				//Remove from left
				getLeft(p,marker) = 0;
			}else {
				//Remove from right
				getRight(p,marker) = 0;
			}
			//Save changes to disk
			p_ref = p;
		}
		//The node is full. Insert the new value and then split this node
		node.Insert(value);
		int medianIdx = node.length/2;
		Key medianValue = node.keys[medianIdx];
		//Values less than median go in left, greater than go in right node (new nodes allocated)
		Reference<Node> leftPtr = allocator->Allocate<Node>();
		Reference<Node> rightPtr = allocator->Allocate<Node>();
		Node left = leftPtr;
		Node right = rightPtr;
		left.length = medianIdx;
		right.length = medianIdx;
		//Perform copy
		nodecopy(left,node,0,0,medianIdx);
		nodecopy(right,node,0,medianIdx+1,medianIdx);
		//Update parents
		fixupParents(leftPtr, rightPtr, left, right);

		left.parent = node.parent;
		right.parent = node.parent;
		medianValue.left = leftPtr.offset;
		medianValue.right = rightPtr.offset;
		//Insert the left and right trees into the parent node
		if(node.parent == 0) {
			//We are at the root, add a new root, with the left and right nodes as children.
			Reference<Node> newRoot = allocator->Allocate<Node>();
			Node nroot = newRoot;
			this->root = newRoot;
			left.parent = newRoot.offset;
			right.parent = newRoot.offset;
			nroot.keys[0] = medianValue.val;
			nroot.children[0] = medianValue.left;
			nroot.children[1] = medianValue.right;
			nroot.length = 1;
			//Update root
			newRoot = nroot;
			//Update left
			leftPtr = left;
			//Update right
			rightPtr = right;
			return;
		}
			Reference<Node> parentPtr = Reference<Node>(allocator->str,left.parent);
			Node parent = parentPtr;
			//Insert the median into the parent (which may cause it to split as well)
			if(medianValue.val == value.val) {
				throw "up";
			}

			//Update nodes
			parentPtr = parent;
			leftPtr = left;
			rightPtr = right;
			Insert(medianValue,parentPtr,true);
			return;

		}
	void Insert(const T& val) {
		Insert(val,root);
	}
	Reference<Node> D(uint64_t offset) {
		return Reference<Node>(allocator->str,offset);
	}
	const uint64_t& getLeft(const Node& node, size_t keyIndex) {
			return node.children[keyIndex];
		}
		const uint64_t& getRight(const Node& node, size_t keyIndex) {
			return node.children[keyIndex+1];
		}
	uint64_t& getLeft(Node& node, size_t keyIndex) {
		return node.children[keyIndex];
	}
	uint64_t& getRight(Node& node, size_t keyIndex) {
		return node.children[keyIndex+1];
	}
	uint64_t FindSibling(Reference<Node> nodePtr, bool& isLeft) {
		Node node = nodePtr;
		Reference<Node> parentPtr = D(node.parent);
		Node parent = parentPtr;
		T key = node.keys[0];
		int marker = 0;
		BinarySearch(parent.keys,parent.length,key,marker);
		if(getLeft(parent,marker) == nodePtr.offset) {
			//We are in left sub-tree
			isLeft = true;
			return getRight(parent,marker);
		}else {
			if(getRight(parent,marker) != nodePtr.offset) {
				printf("Scan problem\n");
				throw "up";
			}
			//We are in right sub-tree
			isLeft = false;
			return getLeft(parent,marker);
		}
	}
	int FindInParent(const Node& searchValue, const Node& parent) {
		int marker = 0;
		T key = searchValue.keys[0];
		BinarySearch(parent.keys,parent.length,key,marker);
		return marker;
	}
	void fixupParents(Reference<Node> nodeptr, Node& node) {
		for(size_t i = 0;i<node.length;i++) {
			if(getLeft(node,i)) {
				Reference<Node> m = D(getLeft(node,i));
				Node a = m;
				if(a.parent !=nodeptr.offset) {
					a.parent = nodeptr.offset;
					m = a;
				}
				m = D(getRight(node,i));
				a = m;
				if(a.parent !=nodeptr.offset) {
					a.parent = nodeptr.offset;
					m = a;
				}
			}

		}
	}
	void Rebalance(Reference<Node> nodePtr) {
		//printf("\nNOTICE:\nREBALANCING DISABLED\nTEMPORARILY FOR TESTING\n");
//return;


		Node node = nodePtr;
		bool isLeft;
		Reference<Node> siblingPtr = D(FindSibling(nodePtr,isLeft));
		if(siblingPtr.offset == 0) {
			printf("Hit root.\n");
			throw "up";
		}
		Node sibling = siblingPtr;
		Reference<Node> parentPtr = D(node.parent);
		Node parent = parentPtr;
		int parentmarker = FindInParent(node,parent);
		if((getLeft(parent,parentmarker) != nodePtr.offset) && (getRight(parent,parentmarker) != nodePtr.offset)) {
			throw "up";
		}
		if(sibling.length>KeyCount/2) {
			//we have a sibling with enough nodes!
			printf("Kids happen\n");
			//TODO: PROBLEM is that we must rotate BOTH keys AND children when performing a rotation
			//as illustrated in this article: http://en.wikipedia.org/wiki/Tree_rotation
			if(isLeft) {
				//Rotate left
				//Losing the children of the siblings

				printf("TODO: CHILDREN ARE GETTING LOST IN ROTATIONS\n");
				//throw "down";
				//Swap the parent with the left node
				node.children[node.length+1]= sibling.children[0];
				std::swap(node.keys[node.length],parent.keys[parentmarker]);
				//Swap the right node with the parent
				std::swap(parent.keys[parentmarker],sibling.keys[0]);
					//TODO: This causes loss of kids. What to do?
				sibling.nodemove(0,1,sibling.length);
				sibling.length--;
				node.length++;
				//TODO: Fixup parents
				fixupParents(nodePtr,node);
			}else {
				//Rotate right
				printf("TODO: CHILDREN ARE GETTING LOST IN ROTATIONS");
				//Fix for lost children
				//throw "down";
				//memmove(node.keys+1,node.keys,node.length*sizeof(node.keys[0]));
				//Make room for the new key
				node.nodemove(1,0,node.length);
				//Pre-emptively move the kid over
				node.children[0] = sibling.children[sibling.length];
				//Put the separator into the node
				std::swap(node.keys[0],parent.keys[parentmarker]);
				//Put the key into the separator
				std::swap(parent.keys[parentmarker],sibling.keys[sibling.length-1]);
				sibling.length--;
				node.length++;
				//TODO: Fixup parents
				fixupParents(nodePtr,node);
			}
			nodePtr = node;
			siblingPtr = sibling;
			parentPtr = parent;
		}else {
			//No sibling can spare a node

				//Merge with a sibling
				//Copy the separator to the end of this node
			if(isLeft) {
				node.keys[node.length] = parent.keys[parentmarker];
			}else {
				node.nodemove(1,0,node.length);
				node.keys[0] = parent.keys[parentmarker];

			}
				node.length++;
				//Copy everything in our sibling node to this node, and free the sibling node
				if(isLeft) {
				nodecopy(node,sibling,node.length,0,sibling.length);
					if(sibling.length == 0) {
					printf("HUHHH???\n");
					throw "up";
				}
				node.length+=sibling.length;
				}else {
					//Move everything over to the right to make room for the left kids
					node.nodemove(sibling.length,0,node.length);
					//Insert the children
					nodecopy(node,sibling,0,0,sibling.length);
					node.length+=sibling.length;
				}
				allocator->Free(siblingPtr);
				if(isLeft) {
				getRight(parent,parentmarker) = 0;
				}else {
					getLeft(parent,parentmarker) = 0;
				}
				//Fixup parents
				nodePtr = node;
				fixupParents(nodePtr,node);
				//Remove the separator from the parent
				//memmove(parent.keys+parentmarker,parent.keys+parentmarker+1,(parent.length-parentmarker)*sizeof(parent.keys[0]));
				parent.nodemove(parentmarker,parentmarker+1,parent.length-parentmarker);
				if(isLeft) {
					parent.children[parentmarker] = nodePtr.offset;
				}else {
					parent.children[parentmarker+1] = nodePtr.offset;
				}
				parent.length--;
				parentPtr = parent;
				//Move the node over to the neighboring element
				if(parent.length == 0 && parent.parent == 0) {
					//We're root.
					//Make ourselves that after freeing parent
					printf("Have you got root? Because I sure do!\n");
					allocator->Free(parentPtr);
					this->root = nodePtr;
					node.parent = 0;
					nodePtr = node;
					return;
				}else {

					if(parent.length<KeyCount/2) {
						//We are below the required number of keys; need to re-balance the parent
						parentPtr = parent;
						nodePtr = node;
						printf("Parent needs rebalancing\n");
						Rebalance(parentPtr);
						parent = parentPtr;
						if(parent.length<KeyCount/2) {
							printf("Still parent problems\n");
						}
					}
				}

		}
	}
	//EXPERIMENTAL DELETE
	void Remove(Reference<Node> nodeptr, size_t keyIndex, std::vector<uint64_t>& rebalancerefs) {
		Node node = nodeptr;
					//If we are a leaf, there is nothing complex that needs to be done here
					//the element can simply be removed.
					if(getLeft(node,keyIndex)) {
						//If we have children we must give them to their parent, then run a fixup
						//Exchange the separator with the greatest value in the leftmost subtree
						Reference<Node> leftPtr = D(getLeft(node,keyIndex));
						Node left = leftPtr;
						findGlobalMax(leftPtr,left);
						T separator = left.keys[left.length-1];
						left.keys[left.length-1] = node.keys[keyIndex];
						node.keys[keyIndex] = separator;
						//Update on-disk layout
						nodeptr = node;
						leftPtr = left;
						//Now; delete the value from the left sub-tree

						Remove(leftPtr,left.length-1,rebalancerefs);
						//Refresh nodes
						left = leftPtr;
						node = nodeptr;

					}
					else {
						//Remove element from node (don't need to worry about children because this is a leaf node)
						node.length--;
						memmove(node.keys + keyIndex, node.keys + keyIndex + 1, (node.length - keyIndex)*sizeof(*node.keys));
						nodeptr = node;

					}
					//Only rebalance if we are root
					if((node.length < KeyCount/2) && node.parent !=0) {
						//rebalancerefs.push_back(nodeptr.offset);
						Rebalance(nodeptr);
						//throw "up";
					}
	}
	bool Delete(const T& value) {
		Reference<Node> nodeptr;
		int keyIndex;
		T val = value;
		if(Find(val,nodeptr,keyIndex)) {
			std::vector<uint64_t> toRebalance;
			Remove(nodeptr,keyIndex,toRebalance);
			for(size_t i = 0;i<toRebalance.size();i++) {
				printf("Rebalancing\n");
				Rebalance(D(toRebalance[i]));
				printf("Rebalanced\n");
			}
			return true;
		}
		return false;
	}
	//END EXPERIMENTAL DELETE





	//Helper functions
	bool Update(const T& value) {
		Reference<Node> nodeptr;
		int keyIndex;
		T val = value;
		if(Find(val,nodeptr,keyIndex)) {
			Node node = nodeptr;
			node.keys[keyIndex] = value;
			nodeptr = node;
			return true;
		}
		return false;
	}
	template<typename F>
	void Traverse(Reference<Node> rootPtr, const F& callback) {
		Node root = rootPtr;
		//printf("PROBLEM IDENTIFIED\n:TREE TRAVERSAL VISITS SAME NODE TWICE:\nHappens because we are only incrementing by one\nbut getting left and right values EACH time.");

		for(int i = 0;i<root.length;i++) {
			//Traverse left sub-tree if exists
			if(getLeft(root,i) && i==0) {
				if(D(getLeft(root,i)).val().parent != rootPtr.offset) {
					throw "down";
				}
				Traverse(D(getLeft(root,i)),callback);

			}
			//Callback
			callback(root.keys[i]);
			//Traverse right sub-tree if exists
			if(getRight(root,i)) {
				if(D(getRight(root,i)).val().parent != rootPtr.offset) {
					throw "sideways";
				}
				Traverse(D(getRight(root,i)),callback);

			}
		}

	}
	template<typename F>
	void Traverse(const F& callback) {
		Traverse(root,callback);
	}
	//End helper functions


};



}



#endif /* MEMORYABSTRACTION_H_ */
