#include <cstdint>
#include <Windows.h>
#include <iostream>
#include <cassert>

/**
* Custom vector implementation using virtual memory
* Team: Alexander Mueller, Stefan Reinhold, Lukas Vogl
* Environment: Windows 64bit, Debug / Release
* Remark: This vector implementation uses virtual memory and supports a upper bound of 1GB of memory per vector
* If you need more you have to adjust the MAX_VECTOR_CAPACITY to fit your needs
**/

/**
 * VirtualMemory namespace is responsible for abstracting platform specific implementations of virtual memory
 * Currently we only support an implementation for Windows
 */
namespace VirtualMemory
{
	void* ReserveAddressSpace(size_t size)
	{
		return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
	}

	//https://msdn.microsoft.com/en-us/library/windows/desktop/aa366892(v=vs.85).aspx
	//If the dwFreeType parameter is MEM_RELEASE, size parameter must be 0 (zero).
	void  FreeAddressSpace(void* from)
	{
		VirtualFree(from, 0u, MEM_RELEASE);
	}

	void* GetPhysicalMemory(void* from, size_t size)
	{
		return VirtualAlloc(from, size, MEM_COMMIT, PAGE_READWRITE);
	}

	void  FreePhysicalMemory(void* from, size_t size)
	{
		VirtualFree(from, size, MEM_DECOMMIT);
	}

	size_t GetPageSize(void)
	{
		SYSTEM_INFO sys_inf;
		GetSystemInfo(&sys_inf);
		return sys_inf.dwPageSize;
	}
}

/**
 * Math namespace is a collection of functions that help with math calculations
 */
namespace MathUtil
{
	size_t roundUpToMultiple(size_t numToRound, size_t multiple)
	{
		if (multiple == 0)
			return numToRound;

		const size_t remainder = numToRound % multiple;
		if (remainder == 0)
			return numToRound;

		return numToRound + multiple - remainder;
	}

	size_t roundDownToMultiple(size_t numToRound, size_t multiple)
	{
		if (multiple == 0)
			return numToRound;

		const size_t remainder = numToRound % multiple;
		if (remainder == 0)
			return numToRound;

		return numToRound - remainder;
	}
}

template <typename T>
class Vector
{
	// Again the neat PointerType union to prevent many casts
	union PointerType
	{
		void* as_void;
		uintptr_t as_ptr;
		T* as_element;
	};

public:
	Vector(void);
	Vector(const Vector<T>& other);
	Vector<T>& operator=(const Vector<T>& other);

	size_t size(void) const;
	size_t capacity(void) const;

	bool empty(void) const;

	void push_back(const T& object);

	void resize(size_t newSize);
	void resize(size_t newSize, const T& object);

	void reserve(size_t newCapacity);

	void erase(size_t index);
	void erase(size_t rangeBegin, size_t rangeEnd);
	void erase_by_swap(size_t index);

	T& operator[] (size_t index);
	const T& operator[] (size_t index) const;

	~Vector(void);

private:

	void GrowByBytes(size_t growSizeInBytes);
	size_t GetGrowSizeInElements(void) const;
	size_t GetMaxElements(void) const;

	size_t m_size;
	size_t m_capacity;
	size_t m_pageSize;

	PointerType m_virtual_mem_begin;
	PointerType m_virtual_mem_end;
	PointerType m_physical_mem_begin;
	PointerType m_physical_mem_end;
	PointerType m_internal_array;

	//Maximum vector capacity as mentioned in lecture - 1GB
	static const size_t MAX_VECTOR_CAPACITY = 1024 * 1024 * 1024;
};

/**
* Constructor without parameters just sets up the internal resources needed in its initializer list
**/
template <typename T>
Vector<T>::Vector()
	: m_size(0u)
	, m_capacity(0u)
	, m_pageSize(VirtualMemory::GetPageSize())
	, m_virtual_mem_begin { VirtualMemory::ReserveAddressSpace(MAX_VECTOR_CAPACITY) }
	, m_virtual_mem_end { reinterpret_cast<void*>(m_virtual_mem_begin.as_ptr + MAX_VECTOR_CAPACITY) }
	, m_physical_mem_begin { m_virtual_mem_begin }
	, m_physical_mem_end { m_virtual_mem_begin }
	, m_internal_array { m_physical_mem_begin }
{}

/**
* Copy Constructor just reserves enough space to hold the content of the other vector and then push_backs the elements
**/
template <typename T>
Vector<T>::Vector(const Vector<T>& other)
	: m_size(0u)
	, m_capacity(0u)
	, m_pageSize(VirtualMemory::GetPageSize())
	, m_virtual_mem_begin { VirtualMemory::ReserveAddressSpace(MAX_VECTOR_CAPACITY) }
	, m_virtual_mem_end { reinterpret_cast<void*>(m_virtual_mem_begin.as_ptr + MAX_VECTOR_CAPACITY) }
	, m_physical_mem_begin { m_virtual_mem_begin }
	, m_physical_mem_end { m_virtual_mem_begin }
	, m_internal_array { m_physical_mem_begin }
{
	reserve(other.m_capacity);
	for (size_t i = 0; i < other.m_size; ++i)
	{
		push_back((other[i]));
	}
}

/**
* The Vector<T> assignment operator - the most discussed piece of code in this exercise :)
* We had three implementations we though about:
* - On assignment, decommitt all pages and reserve the capacity of the other vector, push_back elements
* - On assignment, just decommitt unsused pages (one need to be careful to not accidentially delete more pages by calculating a range that straddles two pages, to make this work we had this impleneation round down to the next smaller pageSize 4098 bytes would have been 4096 to just free the one redundant page)
* - Be std::vector conform and don't shrink to the others vector capacity on assignment (that's what we chose after a long discussion)
* We decided upon the third solution to let the user control when the vector shall release capacity / shrink - we did not
* implement a shrink_to_fit function but we would let the user call it whenever a shrink is requested instead of
* implicitely shrink on assignment
**/
template <typename T>
Vector<T>& Vector<T>::operator=(const Vector<T>& other)
{
	if (this != &other)
	{
		// destruct elements of this vector
		for (size_t i = 0u; i < m_size; ++i)
		{
			m_internal_array.as_element[i].~T();
		}

		// adjust capacity to match other vector only if the others capacity is larger than ours
		// if it is lesser or equal we go with the current capacity and just copy in the others content
		if (other.m_capacity > m_capacity)
		{
			reserve(other.m_capacity);
		}

		// need to set size to 0, so push_back will work correctly
		m_size = 0u;

		// copy everything from the other vector
		for (size_t i = 0; i < other.m_size; ++i)
		{
			push_back((other[i]));
		}
	}

	return *this;
}

/**
* On destruction we call the dtors of all our elements and then release all pages and the
* virtual address space
**/
template <typename T>
Vector<T>::~Vector()
{
	for (size_t i = 0u; i < m_size; ++i)
	{
		m_internal_array.as_element[i].~T();
	}
	VirtualMemory::FreeAddressSpace(m_virtual_mem_begin.as_void);
}

template <typename T>
size_t Vector<T>::size() const
{
	return m_size;
}

template <typename T>
size_t Vector<T>::capacity() const
{
	return m_capacity;
}

template <typename T>
bool Vector<T>::empty() const
{
	return m_size == 0u;
}

/**
* Push_back is responsible for adding a new element to the internal array using placement new
* If the capacity is not big enough to hold the new element the vector grows by allocating new
* memory pages if there is enough address space left to do so. The grow policy is capacity times two 
* at the moment
**/
template <typename T>
void Vector<T>::push_back(const T& object)
{
	if (m_capacity == m_size)
	{
		GrowByBytes(GetGrowSizeInElements() * sizeof(T));
	}

	// if we were not able to grow anymore, the placement new will try to write memory that we don't have
	// and therefore propably crash. This only happens in release mode, because of the missing assert in GrowByBytes().
	// we decided to do this anyways, because it is propably better to let the user crash, than to just do nothing and
	// let the user think everything worked fine when it actually did not.

	PointerType targetPtr;
	targetPtr.as_ptr = m_internal_array.as_ptr + m_size * sizeof(T);
	new (targetPtr.as_void) T(object);

	++m_size;
}

/**
* We also discussed a lot about the duplicated code here in the resize functions but came to the conclusion
* that for us this is the only valid approach we came upon. If we would use an internal_resize(size_t, T*)
* to use the same code and differentiate the construction code via a valid pointer / nullptr then it would also
* not compile because the compiler would already see the copy-constructor in the branch using copy-construction.
* This would force the user to implement a copy-constructor although resize(size_t) was called. We decided it
* is better (but not nice at all) to go with duplicated code instead
**/

/*
 * On a resize request we have 3 possible actions to do:
 * - newSize == m_size: do nothing and we are good
 * - newSize > m_size: We need to expand the vector to hold at least newSize elements, if the capacity fits: good, if not we have to grow
 * - newSize < m_size: We need to destroy elements until m_size fits the newSize, for this we need to call N destructors where N is the
 *                     amount of elements that reside in the vector after newSize. Then we reduce m_size. We don't hand back capacity.
 */
template <typename T>
void Vector<T>::resize(size_t newSize)
{
	{
		bool resizeRequestExceedsAvailableRange = newSize > GetMaxElements();
		assert("Resize requested more elements then the max capacity possible" && !resizeRequestExceedsAvailableRange);
	}

	if (newSize == m_size)
	{
		return;
	}

	if (newSize > m_size) // Add n elements until m_size equals newSize
	{
		if (newSize > m_capacity) // If the capacity is not sufficient, we need to grow
		{
			const size_t growSizeInBytes = (newSize - m_capacity) * sizeof(T);
			GrowByBytes(growSizeInBytes);
		}

		PointerType targetPtr;
		for (size_t i = m_size; i < newSize; ++i)
		{
			targetPtr.as_ptr = m_internal_array.as_ptr + i * sizeof(T);
			// Small optimization here for built-in types. Before we called T() here what we discovered zero-initializes built-in types
			// introducing a very small overhead to default-initialization but it can be measured and therefore gained us some performace
			new (targetPtr.as_void) T;
		}
	}
	else
	{
		//Destruct redundant elements
		for (size_t i = newSize; i < m_size; ++i)
		{
			m_internal_array.as_element[i].~T();
		}
	}
	m_size = newSize;
}

/*
 * This resize overload works just like the resize(size_t) function but with the difference of constructing 
 * the new elements using the copy ctor of the T type and call it with the provided template object
 */
template <typename T>
void Vector<T>::resize(size_t newSize, const T& object)
{
	{
		bool resizeRequestExceedsAvailableRange = newSize > GetMaxElements();
		assert("Resize requested more elements then the max capacity possible" && !resizeRequestExceedsAvailableRange);
	}

	if (newSize == m_size)
	{
		return;
	}

	if (newSize > m_size) // Add n elements until m_size equals newSize
	{
		if (newSize > m_capacity) // If the capacity is not sufficient, we need to grow
		{
			const size_t growSizeInBytes = (newSize - m_capacity) * sizeof(T);
			GrowByBytes(growSizeInBytes);
		}

		PointerType targetPtr;
		for (size_t i = m_size; i < newSize; ++i)
		{
			targetPtr.as_ptr = m_internal_array.as_ptr + i * sizeof(T);
			// Here we call T`s CCTOR with the template object from the parameters
			new (targetPtr.as_void) T(object);
		}
	}
	else
	{
		//Destruct redundant elements
		for (size_t i = newSize; i < m_size; ++i)
		{
			m_internal_array.as_element[i].~T();
		}
	}
	m_size = newSize;
}

/**
 * In reserve(size_t) we try to aquire new resources to fit the requested capacity. If we already have grown big enough
 * we have to do nothing. If we don't fit, we grow the internal array by requesting more physical memory from our
 * preallocated virtual address space.
 */
template <typename T>
void Vector<T>::reserve(size_t newCapacity)
{
	{
		bool capacityRequestExceedsAvailableRange = newCapacity > GetMaxElements();
		assert("Reserve requested more capacity then the max capacity possible" && !capacityRequestExceedsAvailableRange);
	}

	//If already big enough, do nothing
	if (newCapacity <= m_capacity)
	{
		return;
	}

	const size_t growSizeInBytes = (newCapacity - m_capacity) * sizeof(T);
	GrowByBytes(growSizeInBytes);
}

// INFO: All erase functions require T to properly implement the assignment operator and DTOR of the type

/**
 * Erase with one parameter removes the element under this index from the vector. We first check if the index is out of range
 * where we can only do the check for the upper bound because we decided to take a size_t as parameter (no negativ index). We
 * then use the assignment operator of the stored type T to move every element one slot to the front and so we 'bubble up'
 * the element we have the dtor upon to the end. We then call the dtor to the last element and reduce the size
 * 
 * We stick to the complexity behaviour of std::vector
 * that says erase will call DTOR for N where N is the amount of elements to delete and will call Assignment OP M times
 * where M is the amount of elements after the deleted one.
 */
template <typename T>
void Vector<T>::erase(size_t index)
{
	{
		//Check if index is in Range, no negative check needed because size_t is unsigned
		const bool isIndexInRange = index < m_size;
		assert("Index out of Range!" && isIndexInRange);
	}

	for (size_t i = index; i < m_size - 1; ++i)
	{
		PointerType current, next;
		current.as_element = &(m_internal_array.as_element[i]);
		next.as_element = &(m_internal_array.as_element[i + 1]);

		// Assign the next to the current element (assuming the user implemented the assignment operator properly)
		// Also a requirement of std::vector (MoveAssignment) implemented
		*current.as_element = *next.as_element;
	}

	// At the end call the dtor for the last element to free its resources
	m_internal_array.as_element[m_size - 1].~T();
	--m_size;
}

/**
 * EraseRange works just like erase but with the difference that a whole range is cleared.
 * If Begin == End we do nothing.
 */
template <typename T>
void Vector<T>::erase(size_t rangeBegin, size_t rangeEnd)
{
	{
		const bool isEndBiggerThanOrEqualToStart = rangeEnd >= rangeBegin;
		assert("EndIndex needs to be larger than or equal to StartIndex!" && isEndBiggerThanOrEqualToStart);
		const bool isEndInVector = rangeEnd <= m_size - 1;
		assert("EndIndex is out of vector range" && isEndInVector);
	}
	
	// Quote: The iterator first does not need to be dereferenceable if first==last: erasing an empty range is a no-op.
	// Comes from erasing ranges with iterator begin() and end()
	// If begin == end means begin is not dereferencable and can not be deleted -> no-op
	if (rangeBegin != rangeEnd)
	{
		// Erasing a range needs to bubble up a group of holes
		// To do so we check how many elements shall be deleted and offset the index of the loop by this
		// to assign a still valid object to an invalid hole.
		size_t elementsToDelete = rangeEnd - rangeBegin + 1;
		for (size_t i = rangeBegin; i < m_size - elementsToDelete; ++i)
		{
			PointerType current, next;
			current.as_element = &(m_internal_array.as_element[i]);
			next.as_element = &(m_internal_array.as_element[i + elementsToDelete]);

			// Assign the next to the current element (assuming the user implemented the asignment operator properly)
			// Also a requirement of std::vector (MoveAssignment) implemented
			*current.as_element = *next.as_element;
		}

		// Now delete the bubbled up elements that would leak resources if the dtor was not called
		for (size_t i = m_size - elementsToDelete; i < m_size; ++i)
		{
			m_internal_array.as_element[i].~T();
		}

		m_size -= elementsToDelete;
	}
}

/**
 * Erase by swap is a little more performant than erase because it just calls the assignment OP once to 
 * assign the last element to the one to delete and then calls the DTOR of the last element to free the resources
 */
template <typename T>
void Vector<T>::erase_by_swap(size_t index)
{
	{
		//Check if index is in Range, no negative check needed because size_t is unsigned
		const bool isIndexInRange = index < m_size;
		assert("Index out of Range!" && isIndexInRange);
	}

	PointerType lastElement;
	lastElement.as_element = &(m_internal_array.as_element[m_size - 1]);

	if (index < m_size - 1) 
	{
		PointerType toDelete;
		toDelete.as_element = &(m_internal_array.as_element[index]);
		*toDelete.as_element = *lastElement.as_element;
	}

	lastElement.as_element->~T();
	--m_size;
}

template <typename T>
T& Vector<T>::operator[](size_t index)
{
	//No check for >= 0 needed because index is unsigned!
	assert("Subscript out of range!" && index < m_size);
	return m_internal_array.as_element[index];
}

template <typename T>
const T& Vector<T>::operator[](size_t index) const
{
	//No check for >= 0 needed because index is unsigned!
	assert("Subscript out of range!" && index < m_size);
	return m_internal_array.as_element[index];
}

/**
 * GrowByBytes is an internal function used to get more physical memory for the
 * prereserved virtual address space. 
 */
template <typename T>
void Vector<T>::GrowByBytes(size_t growSizeInBytes)
{
	if (growSizeInBytes == 0u) return; // Grows by 0 are just rejected
	
	// Round up to the next highest multiple of the current OS page size
	size_t roundedGrowSize = MathUtil::roundUpToMultiple(growSizeInBytes, m_pageSize);

	{
		// If the grow would exceed the available address space we cannot grow anymore
		// this happends if the m_physical_mem pointer is already at the m_virtual_end
		const bool addressSpaceEndReached = m_physical_mem_end.as_ptr == m_virtual_mem_end.as_ptr;
		assert("Grow would exceed maximum available address space - cannot grow further!" && !addressSpaceEndReached);
	}

	// We though about this and decided it makes sense that if a user
	// push_backs into the vector and the grow behaviour would exceed the range
	// then we allow growing to the maximum available address space and just fail to
	// grow if we really are out of memory
	if (m_physical_mem_end.as_ptr + roundedGrowSize > m_virtual_mem_end.as_ptr)
	{
		size_t remainingGrowSpace = m_virtual_mem_end.as_ptr - m_physical_mem_end.as_ptr;
		roundedGrowSize = MathUtil::roundDownToMultiple(remainingGrowSpace, m_pageSize);
	}

	PointerType allocation;
	allocation.as_void = VirtualMemory::GetPhysicalMemory(m_physical_mem_end.as_void, roundedGrowSize);
	m_physical_mem_end.as_ptr = allocation.as_ptr + roundedGrowSize;
	// If the range is not equally divisable by the sizeof(T) this implicitely does a floor(...)
	// and we are good because we don't say that we have more capacity than we really have
	m_capacity = (m_physical_mem_end.as_ptr - m_physical_mem_begin.as_ptr) / sizeof(T);
}

template <typename T>
size_t Vector<T>::GetGrowSizeInElements() const
{
	// This is a small trick we found in a blog and thought about a bit
	// If we allocate one element it is very probable that we allocate a few more and 
	// it shows a small performance gain when allocating 8 slots at the beginning instead of going 1-2-4-8 for the first few push_backs
	// INFO: This is a better optimization for a non virtual mem based vector implementation but we leave it here as a reference to think
	// about this kind of micro-opts when virtual mem would not be a thing (thank `eternal thing` it is)
	return m_capacity ? m_capacity * 2 : 8;
}

/**
* Convenient function to retrieve the maximum amount of elements this vector can ever hold
**/
template<typename T>
size_t Vector<T>::GetMaxElements(void) const
{
	return MAX_VECTOR_CAPACITY / sizeof(T);
}

/// ++++++++++++++++++++++++++++++++++++++++++
/// ++++++++++++++ TEST PROGRAM ++++++++++++++
/// ++++++++++++++++++++++++++++++++++++++++++

namespace UnitTests
{
	void Construction()
	{
		Vector<int> intVec;
		assert("Initial capacity was not 0" && intVec.capacity() == 0);
		assert("Initial size was not 0" && intVec.size() == 0);
	}

	void CopyConstruction()
	{
		Vector<size_t> firstVector;

		firstVector.push_back(123u);
		firstVector.push_back(456u);
		firstVector.push_back(789u);
		firstVector.push_back(123456789u);

		Vector<size_t> testVector(firstVector);
		assert("Vector size mismatch" && firstVector.size() == testVector.size());
		assert("Vector capacity mismatch" && firstVector.capacity() == testVector.capacity());

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);
	}

	void Assignment()
	{
		Vector<size_t> smallVector;
		smallVector.push_back(123u);
		smallVector.push_back(456u);
		smallVector.push_back(789u);

		Vector<size_t> mediumVector;
		mediumVector.push_back(13u);
		mediumVector.push_back(57u);
		mediumVector.push_back(911u);
		mediumVector.push_back(24u);
		mediumVector.push_back(68u);
		mediumVector.push_back(1012u);

		Vector<size_t> largeVector;
		largeVector.push_back(312u);
		largeVector.push_back(654u);
		largeVector.push_back(987u);
		largeVector.push_back(121110u);
		largeVector.push_back(151413u);
		largeVector.push_back(181716u);
		largeVector.push_back(212019u);
		largeVector.push_back(242322u);
		largeVector.push_back(272625u);

		// test assignment of larger vector
		mediumVector = largeVector;
		assert("Vector size mismatch" && mediumVector.size() == largeVector.size());
		assert("Vector capacity mismatch" && mediumVector.capacity() == largeVector.capacity());

		assert(mediumVector[0] == 312u);
		assert(mediumVector[1] == 654u);
		assert(mediumVector[2] == 987u);
		assert(mediumVector[3] == 121110u);
		assert(mediumVector[4] == 151413u);
		assert(mediumVector[5] == 181716u);
		assert(mediumVector[6] == 212019u);
		assert(mediumVector[7] == 242322u);
		assert(mediumVector[8] == 272625u);

		// test assignment of smaller vector
		largeVector = smallVector;
		assert("Vector size mismatch" && largeVector.size() == smallVector.size());
		assert("Vector capacity mismatch" && largeVector.capacity() == smallVector.capacity());

		assert(smallVector[0] == 123u);
		assert(smallVector[1] == 456u);
		assert(smallVector[2] == 789u);
	}

	void PushBack()
	{
		Vector<size_t> firstVec;

		for (size_t i = 0; i < 5; ++i)
		{
			firstVec.push_back(i);
		}

		assert("Size should equal 5" && firstVec.size() == 5);

		for (size_t i = 0; i < 5; ++i)
		{
			assert("Vector value mismatch" && firstVec[i] == i);
		}
	}

	void PushBackDepleteResources()
	{
		Vector<size_t> v;

		// Vector can hold 134217727 size_ts on 64 bit in 1GB mem
		for (size_t i = 0; i < 134217728; ++i)
		{
			v.push_back(0u);
		}

		// Pushing another element would exceed the available address space and asserts
		v.push_back(666u);
	}

	void Reserve()
	{
		Vector<int> vec;
		// We want to reserve spce for 100 ints (100 * 4 = 400 bytes mem), because we use VirtualMem
		// we can only allocate in page size chunks and will reserve here at least 4KB (4096 bytes) mem, so the
		// capacity shall be 1024 after the reserve call
		vec.reserve(100);
		assert("Capacity did not match the expected grow behaviour" && vec.capacity() == 1024);
	}

	void TooBigReserve()
	{
		Vector<size_t> v;
		// 1GB on 64bit could hold 134217728 size_ts
		v.reserve(134217729); // A reserve request that would exceed the max_capacity asserts
	}

	void ResizeDefaultCtor(size_t initialSize, size_t resizeSize)
	{
		Vector<size_t> vec;
		for (size_t i = 0; i < initialSize; ++i)
		{
			vec.push_back(i);
		}
		vec.resize(resizeSize);
		assert("Vector size did not changed as requested" && vec.size() == resizeSize);
	}

	void ResizeBigDefaultCtor()
	{
		Vector<int> testVector;
		testVector.resize(2500, 0xDEADBEEF);

		assert(testVector.size() == 2500);
		const size_t capacity = testVector.capacity();

		testVector.resize(500);

		assert(testVector.size() == 500);
		assert(testVector.capacity() == capacity);
	}

	void ResizeWithValue(size_t initialSize, size_t resizeSize)
	{
		Vector<size_t> vec;
		for (size_t i = 0; i < initialSize; ++i)
		{
			vec.push_back(i);
		}
		vec.resize(resizeSize, 5);

		if (resizeSize > initialSize)
		{
			for (size_t i = initialSize; i < resizeSize; ++i)
			{
				assert("Resize did not fill elements with requested default value" && vec[i] == 5);
			}
		}

		assert("Vector size did not change as requested" && vec.size() == resizeSize);
	}

	void EraseSingle()
	{
		Vector<size_t> testVector;

		testVector.push_back(123u);
		testVector.push_back(456u);
		testVector.push_back(789u);
		testVector.push_back(123456789u);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);

		testVector.erase(1);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 789u);
		assert(testVector[2] == 123456789u);
		assert(testVector.size() == 3u);
	}

	void EraseRange()
	{
		Vector<size_t> testVector;

		testVector.push_back(123u);
		testVector.push_back(456u);
		testVector.push_back(789u);
		testVector.push_back(123456789u);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);

		testVector.erase(1, 2);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 123456789u);
		assert(testVector.size() == 2u);
	}

	void EraseEmptyRange()
	{
		Vector<size_t> testVector;

		testVector.push_back(123u);
		testVector.push_back(456u);
		testVector.push_back(789u);
		testVector.push_back(123456789u);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);

		testVector.erase(1, 1);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);
	}

	void EraseBySwap()
	{
		Vector<size_t> testVector;

		testVector.push_back(123u);
		testVector.push_back(456u);
		testVector.push_back(789u);
		testVector.push_back(123456789u);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);

		testVector.erase_by_swap(1);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 123456789u);
		assert(testVector[2] == 789u);
		assert(testVector.size() == 3u);
	}

	namespace CustomTypes
	{
		struct ClassWithoutDefaultCTOR
		{
			ClassWithoutDefaultCTOR(int bar) : foo(bar) {}
			int foo;
		};

		struct ClassWithoutCCTOR
		{
			ClassWithoutCCTOR() {}
			ClassWithoutCCTOR(const ClassWithoutCCTOR& other) = delete;
		};

		struct ClassWithoutAssignmentOP
		{
			ClassWithoutAssignmentOP() {}
			ClassWithoutAssignmentOP(const ClassWithoutAssignmentOP* other) {}
			ClassWithoutAssignmentOP& operator=(const ClassWithoutAssignmentOP& other) = delete;
		};

		class Custom
		{
		public:
			static size_t CustomCTORCount;
			static size_t CustomCCTORCount;
			static size_t CustomAssignmentCount;
			static size_t CustomDTORCount;

			Custom() 
				: data(0)
			{
				++CustomCTORCount;
			}

			Custom(int d)
				: data(d)
			{}

			Custom(const Custom& other)
			{
				++CustomCCTORCount; 
				data = other.data;
			}

			Custom& operator=(const Custom& other) 
			{ 
				++CustomAssignmentCount;
				if (&other != this)
				{
					data = other.data;
				} 
				return *this; 
			}

			~Custom()
			{
				++CustomDTORCount;
			}

			size_t data;
		};

		void ResetStaticCounters()
		{
			Custom::CustomCTORCount = 0;
			Custom::CustomDTORCount = 0;
			Custom::CustomCCTORCount = 0;
			Custom::CustomAssignmentCount = 0;
		}

		size_t Custom::CustomCTORCount = 0;
		size_t Custom::CustomDTORCount = 0;
		size_t Custom::CustomCCTORCount = 0;
		size_t Custom::CustomAssignmentCount = 0;

		void TestPushBack()
		{
			Vector<Custom> firstVec;

			for (int i = 0; i < 5; ++i)
			{
				Custom temp(i);
				firstVec.push_back(temp);
			}

			assert("Size should equal 5" && firstVec.size() == 5);

			for (size_t i = 0; i < 5; ++i)
			{
				assert("Vector value mismatch" && firstVec[i].data == i);
			}
		}

		void TestResizeDefaultCTOR(size_t initialSize, size_t resizeSize)
		{
			ResetStaticCounters();

			Vector<Custom> vec;
			vec.resize(initialSize);

			ResetStaticCounters();

			vec.resize(resizeSize);
			assert("Vector size did not change as requested" && vec.size() == resizeSize);
			if (initialSize > resizeSize)
			{
				assert("Default DTOR was not called sufficient times" && Custom::CustomDTORCount == (initialSize - resizeSize));
			}
			else
			{
				assert("Default CTOR was not called sufficient times" && Custom::CustomCTORCount == (resizeSize - initialSize));
			}
		}

		void TestResizeWithCCTOR(size_t initialSize, size_t resizeSize)
		{
			ResetStaticCounters();

			Vector<Custom> vec;
			vec.resize(initialSize);

			ResetStaticCounters();

			Custom initializer; 
			initializer.data = 0xA;

			vec.resize(resizeSize, initializer);
			assert("Vector size did not change as requested" && vec.size() == resizeSize);
			if (initialSize > resizeSize)
			{
				assert("Default DTOR was not called sufficient times" && Custom::CustomDTORCount == (initialSize - resizeSize));
			}
			else
			{
				assert("CCTOR was not called sufficient times" && Custom::CustomCCTORCount == (resizeSize - initialSize));
				for (size_t i = initialSize; i < resizeSize; ++i)
				{
					assert("Resize did not fill elements with requested default value" && vec[i].data == 0xA);
				}
			}
		}

		void TestErase()
		{
			ResetStaticCounters();

			Vector<Custom> customVec;
			customVec.resize(6);
			customVec[0].data = 12u;
			customVec[1].data = 34u;
			customVec[2].data = 56u;
			customVec[3].data = 78u;
			customVec[4].data = 90u;
			customVec[5].data = 1122u;

			customVec.erase(1);

			assert("No DTOR was called for erased object" && Custom::CustomDTORCount == 1);
			assert("Assignment operators were not called sufficient times" && Custom::CustomAssignmentCount == 4);
			assert(customVec[0].data == 12u);
			assert(customVec[1].data == 56u);
			assert(customVec[2].data == 78u);
			assert(customVec[3].data == 90u);
			assert(customVec[4].data == 1122u);
		}

		void TestEraseBySwap()
		{
			ResetStaticCounters();

			Vector<Custom> customVec;
			customVec.resize(6);
			customVec[0].data = 12u;
			customVec[1].data = 34u;
			customVec[2].data = 56u;
			customVec[3].data = 78u;
			customVec[4].data = 90u;
			customVec[5].data = 1122u;

			customVec.erase_by_swap(1);

			assert("No DTOR was called for erased object" && Custom::CustomDTORCount == 1);
			assert("Assignment operators was called more than once" && Custom::CustomAssignmentCount == 1);
			assert(customVec[0].data == 12u);
			assert(customVec[1].data == 1122u);
			assert(customVec[2].data == 56u);
			assert(customVec[3].data == 78u);
			assert(customVec[4].data == 90u);
		}

		void TestEraseRange()
		{
			ResetStaticCounters();

			Vector<Custom> customVec;
			customVec.resize(4);
			customVec[0].data = 123u;
			customVec[1].data = 456u;
			customVec[2].data = 789u;
			customVec[3].data = 123456789u;

			customVec.erase(1, 2);

			assert("No DTOR was called for erased object" && Custom::CustomDTORCount == 2);
			assert("Assignment operators were not called sufficient times" && Custom::CustomAssignmentCount == 1);
			assert(customVec[0].data == 123u);
			assert(customVec[1].data == 123456789u);
		}

		void TestDTORCalls()
		{
			ResetStaticCounters();

			{
				Vector<Custom> customVec;
				customVec.resize(100);
			}

			assert("DTOR was not called for all elements" && Custom::CustomDTORCount == 100);
		}

		void TestAssignment()
		{
			ResetStaticCounters();

			// We need a slightly bigger vector than the one to copy 
			// to really have different capacities
			Vector<Custom> customVecLarge;
			customVecLarge.resize(1000);

			for (size_t i = 0; i < 1000; ++i)
			{
				customVecLarge[i].data = 1122u;
			}

			Vector<Custom> customVecSmall;
			customVecSmall.resize(2);
			customVecSmall[0].data = 987u;
			customVecSmall[1].data = 654u;

			size_t oldCapacity = customVecLarge.capacity();
			customVecLarge = customVecSmall;

			assert("DTOR was not called for all elements" && Custom::CustomDTORCount == 1000);

			assert("Vector size mismatch" && customVecLarge.size() == customVecSmall.size());
			assert("Vector capacity mismatch" && customVecLarge.capacity() == oldCapacity);

			assert(customVecLarge[0].data == 987u);
			assert(customVecLarge[1].data == 654u);
		}

		// This test was used to test odd sized types because we had a gut feeling
		// that this could cause some edge cases on assignment when some data stradles a 
		// page boundary. It turned out it did when we had the assignment operator implementation
		// that kicked out unsued memory pages. Now stays here as a relict of some cool but non
		// std::vector conform implementation (someone has lost the discussion)
		void TestAssignmentOdd()
		{
			struct SixByte
			{
				bool one;
				bool two;
				bool three;
				bool four;
				bool five;
				bool six;
			};

			ResetStaticCounters();

			// We need a slightly bigger vector than the one to copy 
			// to really have different capacities
			Vector<SixByte> customVecLarge;
			customVecLarge.resize(1000);

			Vector<SixByte> customVecSmall;
			customVecSmall.resize(2);

			size_t oldCapacity = customVecLarge.capacity();
			customVecLarge = customVecSmall;

			assert("Vector size mismatch" && customVecLarge.size() == customVecSmall.size());
			assert("Vector capacity mismatch" && customVecLarge.capacity() == oldCapacity);
		}

		// Uncomment to see compile error on using a vec resize without a default ctor
		//void NonDefaultCTOR()
		//{
		//	Vector<ClassWithoutDefaultCTOR> nonDefaultCtorVec;
		//	nonDefaultCtorVec.push_back(ClassWithoutDefaultCTOR(1));
		//	nonDefaultCtorVec.push_back(ClassWithoutDefaultCTOR(2));
		//	nonDefaultCtorVec.push_back(ClassWithoutDefaultCTOR(3));
		//	assert("Size does not match" && nonDefaultCtorVec.size() == 3);

		//	nonDefaultCtorVec.resize(10, ClassWithoutDefaultCTOR(12));
		//	assert("Resize size does not match" && nonDefaultCtorVec.size() == 10);
		//	// This shall not compile (and it will not)
		//	nonDefaultCtorVec.resize(10);
		//}

		// Uncomment to see compile error on push_back with deleted cctor
		//void NoCCTOR()
		//{
		//	// No copy-ctor, no push_back
		//	Vector<ClassWithoutCCTOR> nonDefaultCtorVec;
		//	nonDefaultCtorVec.push_back(ClassWithoutCCTOR());
		//	nonDefaultCtorVec.push_back(ClassWithoutCCTOR());
		//	nonDefaultCtorVec.push_back(ClassWithoutCCTOR());
		//	assert("Size does not match" && nonDefaultCtorVec.size() == 3);
		//}

		//void NoAssignmentOP()
		//{
		//	Vector<ClassWithoutAssignmentOP> nonAssignmetnVec;
		//	nonAssignmetnVec.push_back(ClassWithoutAssignmentOP());
		//	nonAssignmetnVec.push_back(ClassWithoutAssignmentOP());
		//	nonAssignmetnVec.push_back(ClassWithoutAssignmentOP());
		//	assert("Size does not match" && nonAssignmetnVec.size() == 3);

		//	nonAssignmetnVec.erase(1);
		//}
	}

	template<class T>
	void DefaultInit()
	{
		// default initialization
		T* a = new T;
		printf("Default initialized T at: %p\n", a);
		assert("Int variable was default initialized" && *a != 0);
	}

	template<class T>
	void ZeroInit()
	{
		// zero initialization
		T* b = new T();
		printf("Zero initialized T at: %p\n", b);
		assert("Int variable was zero initialized" && *b == 0);
	}
}

template <class A>
void someF(A* some)
{
	A* temp = new A();
	if (some == nullptr)
	{
		temp = new (temp)A;
	}
	else
	{
		temp = new(temp)A(*some);
	}
}

int main()
{
	// Standard tets using vectors of int / size_t
	UnitTests::Construction();
	UnitTests::CopyConstruction();
	UnitTests::Assignment();

	UnitTests::PushBack();
	// Uncomment this test to see how the vetor reacts upon push_backs that deplete the resources - takes some time in debug
	// UnitTests::PushBackDepleteResources();
	UnitTests::Reserve();
	// Uncomment this test to see how the vector reacts upon a reserve that would exceed the max capacity - takes some time in debug
	// UnitTests::TooBigReserve();

	UnitTests::ResizeDefaultCtor(0, 10);
	UnitTests::ResizeDefaultCtor(10, 10);
	UnitTests::ResizeDefaultCtor(10, 5);
	UnitTests::ResizeDefaultCtor(10, 20);
	UnitTests::ResizeBigDefaultCtor();

	UnitTests::ResizeWithValue(0, 10);
	UnitTests::ResizeWithValue(10, 10);
	UnitTests::ResizeWithValue(10, 5);
	UnitTests::ResizeWithValue(10, 20);

	UnitTests::EraseSingle();
	UnitTests::EraseBySwap();
	UnitTests::EraseRange();
	UnitTests::EraseEmptyRange();

	// Tests with a CustomType start here
	UnitTests::CustomTypes::TestPushBack();

	UnitTests::CustomTypes::TestResizeDefaultCTOR(0, 10);
	UnitTests::CustomTypes::TestResizeDefaultCTOR(10, 5);
	UnitTests::CustomTypes::TestResizeDefaultCTOR(10, 10);
	UnitTests::CustomTypes::TestResizeDefaultCTOR(10, 20);

	UnitTests::CustomTypes::TestResizeWithCCTOR(0, 10);
	UnitTests::CustomTypes::TestResizeWithCCTOR(10, 5);
	UnitTests::CustomTypes::TestResizeWithCCTOR(10, 10);
	UnitTests::CustomTypes::TestResizeWithCCTOR(10, 20);

	UnitTests::CustomTypes::TestDTORCalls();
	UnitTests::CustomTypes::TestAssignment();
	UnitTests::CustomTypes::TestAssignmentOdd();
	UnitTests::CustomTypes::TestErase();
	UnitTests::CustomTypes::TestEraseBySwap();
	UnitTests::CustomTypes::TestEraseRange();

	// Uncomment these functions in the UnitTest suite to see the compile errors they are generating
	// The are only referenced here to show that they exist
	// UnitTests::CustomTypes::NonDefaultCTOR();
	// UnitTests::CustomTypes::NoCCTOR();
	// UnitTests::CustomTupes::NoAssignmentOP();

	// Only a small test to see the difference between T() and T calls
	// they print addresses that can be inspected in the memory window to show what happens after each call
	// One is cleared to 0's the other is not
	// T() vs T ctor call tests (zero initialization vs. default initialization)
	UnitTests::DefaultInit<int>();
	UnitTests::ZeroInit<int>();

	printf("All Tests done!\n");
}

