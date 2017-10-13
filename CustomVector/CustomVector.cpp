/*
 * CustomVector class as std::vector replacement
 * Authors: Alexander Müller, Stefan Reinhold, Lukas Vogl
 */
#include <Windows.h>
#include <cstdio>
#include <cassert>
#include <new>
#include <cstdint>

//TODO Maybe abstract the Virtual Memory from the Vector implementation with myVirtualMalloc()
namespace VirtualUnicornStuff
{
	/**
	* Round up to get to next multiple of pagesize
	*/
	static size_t roundUp(size_t numToRound, size_t multiple)
	{
		if (multiple == 0)
			return numToRound;

		const size_t remainder = numToRound % multiple;
		if (remainder == 0)
			return numToRound;

		return numToRound + multiple - remainder;
	}
}

template <class T>
class Vector
{

	union PointerType
	{
		void* as_void;
		uintptr_t as_ptr;
		T* as_element;
	};

public:
	// Get some initial memory (maybe ?)
	Vector();

	// At this point we have to call the dtors of the elements stored in the vector
	// and release the capacity bytes memory used
	~Vector();

	// Return the current size of the vector (element count)
	size_t size(void) const;

	// Return the allocated size if the vector
	size_t capacity(void) const;

	// Returns wether vector is empty or not
	bool empty() const;

	// Push back the element into the vector
	// If capacity is still usable copy the content of object to the next free slot
	// If capacity is depleted, regrow the internal storage by a efficient pattern (std::vector does * 2)
	void push_back(const T& object);
	// Resize the internal storage of the vector 
	// * if n < el_size:		shrink to fit n (and destruct all elements on the way)
	// * if n > el_size:		assign values and construct
	// * if n > allocatiosize:	reallocate more mem till n is reached
	void resize(size_t n, T object = T());

	// Reserve memory in the internal storage to fit at least n elements
	// If n is lesser than the capacity nothing is done
	// Reserve shall not alter the containers element
	void reserve(size_t n);

	// 
	void erase(size_t index);

	// Erase by swap shall work in O(1) by 
	// * Delete the element at index (dtor)
	// * Copy the last to its place (placement new + cctor)
	// * Delete the element at the copied location (dtor)
	void erase_by_swap(size_t index);

	// Subscript operator to access element at specific location/index
	// * returns a reference to make it changeable
	T& operator[] (size_t index);

private:
	void Grow();

	PointerType m_internal_array;
	size_t m_size;
	size_t m_capacity;

	PointerType m_virtual_memory_begin;
	PointerType m_virtual_memory_end;
	PointerType m_committed_memory_begin;
	PointerType m_committed_memory_end;
	size_t m_pageSize;

	//Maximum vector capacity as mentioned in lecture - 1GB
	static const size_t MAX_VECTOR_CAPACITY = 1024 * 1024 * 1024;
};

template <class T>
Vector<T>::Vector()
	: m_size(0u)
	, m_capacity(0u)
{
	SYSTEM_INFO sys_inf;
	GetSystemInfo(&sys_inf);

	m_pageSize = sys_inf.dwPageSize;

	// Get a block of 1GB MB virtual address space from the OS 
	m_virtual_memory_begin.as_void = VirtualAlloc(nullptr, MAX_VECTOR_CAPACITY, MEM_RESERVE, PAGE_NOACCESS);
	m_virtual_memory_end.as_ptr = m_virtual_memory_begin.as_ptr + MAX_VECTOR_CAPACITY;

	//Initialize committed memory to be empty
	m_committed_memory_begin = m_virtual_memory_begin;
	m_committed_memory_end = m_committed_memory_begin;

	m_internal_array = m_committed_memory_begin;
}

template <class T>
Vector<T>::~Vector()
{
	const ptrdiff_t memory_size = m_virtual_memory_end.as_ptr - m_virtual_memory_begin.as_ptr;
	VirtualFree(m_virtual_memory_begin.as_void, memory_size, MEM_FREE);
}

template <class T>
size_t Vector<T>::size() const
{
	return m_size;
}

template <class T>
size_t Vector<T>::capacity() const
{
	return m_capacity;
}

template<class T>
bool Vector<T>::empty() const
{
	return size() == 0u;
}

template <class T>
void Vector<T>::push_back(const T& object)
{
	if(m_capacity == m_size)
	{
		Grow();
	}

	PointerType targetPtr;
	targetPtr.as_ptr = m_internal_array.as_ptr + m_size * sizeof(T);
	new (targetPtr.as_void) T(object);
	
	++m_size;
}

template <class T>
void Vector<T>::resize(size_t n, T object = T())
{

}

template <class T>
void Vector<T>::reserve(size_t n)
{

}

template <class T>
void Vector<T>::erase(size_t index)
{
	PointerType destination;
	destination.as_element = &(m_internal_array.as_element[index]);

	destination.as_element->~T();

	//if not last element, close empty slot
	if(index < m_size - 1)
	{
		PointerType source;
		source.as_ptr = destination.as_ptr + sizeof(T);
		memmove(destination.as_void, source.as_void, (m_size - 1 - index) * sizeof(T));
	}
	
	--m_size;
}

template <class T>
void Vector<T>::erase_by_swap(size_t index)
{
	PointerType destination;
	destination.as_element = &(m_internal_array.as_element[index]);

	destination.as_element->~T();

	//if not last element, close empty slot
	if (index < m_size - 1)
	{
		PointerType source;
		source.as_ptr = m_internal_array.as_ptr + (m_size - 1) * sizeof(T);
		memmove(destination.as_void, source.as_void, sizeof(T));
	}

	--m_size;
}

template <class T>
T& Vector<T>::operator[](const size_t index)
{
	//No check for >= 0 needed because index is unsigned!
	assert("Subscript out of range!" && index < m_size);
	return m_internal_array.as_element[index];
}

template<class T>
void Vector<T>::Grow()
{
	size_t growSize = size() * sizeof(T); 

	//Calculate growSize
	if(growSize != 0u)
	{
		growSize = VirtualUnicornStuff::roundUp(growSize, m_pageSize);
	} else
	{
		growSize = VirtualUnicornStuff::roundUp(sizeof(T), m_pageSize);
	}
	
	//TODO Edge Case: Growing from 501MB would end over 1GB but half of vector is unused
	//Solution: Allow Growing once more to maximum of 1GB, but next time assert
	if(m_committed_memory_end.as_ptr + growSize > m_virtual_memory_end.as_ptr)
	{
		growSize = m_virtual_memory_end.as_ptr - m_committed_memory_end.as_ptr;
	}

	{
		//Maybe allow growing virtual address space to ensure endless growing?
		const bool isEndOfVirtualMemoryReached = m_committed_memory_end.as_ptr == m_virtual_memory_end.as_ptr;
		assert("Maximum Capacity reached! Vector can not grow further." && !isEndOfVirtualMemoryReached);
	}
	
	{
		PointerType newPage;
		//Add new Pages to end of committed memory
		newPage.as_void = VirtualAlloc(m_committed_memory_end.as_void, growSize, MEM_COMMIT, PAGE_READWRITE);
		m_committed_memory_end.as_ptr = newPage.as_ptr + growSize;
	}
	
	m_capacity = (m_committed_memory_end.as_ptr - m_committed_memory_begin.as_ptr) / sizeof(T);
}

/*
 *	#################### Test Application ####################
 */

void TestPushBack(size_t count)
{
	Vector<size_t> testVector;

	size_t i = 0u;
	for(i = 0u; i < count; ++i)
	{
		testVector.push_back(i);
	}

	i = testVector.size();
	do
	{
		--i;
		const bool indexEqualsValue = testVector[i] == i;
		assert("Could not verify values in Vector!" && indexEqualsValue);
	} while (i != 0);

	printf("TestPushBack with count %llu done!\n", count);
}

// index parameter is int to allow testing with negative subscript
void TestSubscript(int index)
{
	Vector<size_t> testVector;

	testVector[index] = 0;
}

void TestErase()
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

	printf("Erase Test done!\n");
}

void TestEraseBySwap()
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

	printf("Erase By Swap Test done!\n");
}

int main ()
{
	TestPushBack(100000);

	//TestSubscript(-1);
	//TestSubscript(0);

	TestErase();
	TestEraseBySwap();

	printf("All Tests done!\n");
}
