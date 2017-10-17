#include <cstdint>
#include <Windows.h>
#include <vector>
#include <iostream>
#include <cassert>

/**
 * VirtualMemory namespace is responsible for abstracting platform specific implemenations of virtual memory
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
 * Math namespace is a collection of functions that help with math calculations / helpers
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
}

template <typename T>
class Vector
{
	union PointerType
	{
		void* as_void;
		uintptr_t as_ptr;
		T* as_element;
	};

public:
	Vector(void);
	Vector(const Vector<T>& other);

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

	size_t m_size;
	size_t m_capacity;
	size_t m_pageSize;

	PointerType m_virtual_mem_begin;
	PointerType m_virtual_mem_end;
	PointerType m_physical_mem_begin;
	PointerType m_physical_mem_end;
	PointerType m_internal_array;

	//Maximum vector capacity as mentioned in lecture - 1GB
	//TODO Find out if we want to grow adress space too
	static const size_t MAX_VECTOR_CAPACITY = 1024 * 1024 * 1024;
};

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

template <typename T>
Vector<T>::Vector(const Vector& other)
	: m_size(0u)
	, m_capacity(0u)
	, m_pageSize(VirtualMemory::GetPageSize())
	, m_virtual_mem_begin{ VirtualMemory::ReserveAddressSpace(MAX_VECTOR_CAPACITY) }
	, m_virtual_mem_end{ reinterpret_cast<void*>(m_virtual_mem_begin.as_ptr + MAX_VECTOR_CAPACITY) }
	, m_physical_mem_begin{ m_virtual_mem_begin }
	, m_physical_mem_end{ m_virtual_mem_begin }
	, m_internal_array{ m_physical_mem_begin }
{
	reserve(other.m_capacity);
	for (size_t i = 0; i < other.m_size; ++i)
	{
		push_back((other[i]));
	}
}

template <typename T>
Vector<T>::~Vector()
{
	for (size_t i = 0u; i < m_size; ++i)
	{
		const T* elementToDestruct = &(m_internal_array.as_element[i]);
		//Destructor called by pointer because of possible virtual destructor (polymorphism)
		elementToDestruct->~T();
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

template <typename T>
void Vector<T>::push_back(const T& object)
{
	if (m_capacity == m_size)
	{
		GrowByBytes(GetGrowSizeInElements() * sizeof(T));
	}

	PointerType targetPtr;
	targetPtr.as_ptr = m_internal_array.as_ptr + m_size * sizeof(T);
	new (targetPtr.as_void) T(object);

	++m_size;
}

template <typename T>
void Vector<T>::resize(size_t newSize)
{
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
			// introducind a very small overhead to default-initialization but it can be measured and therefore gained us some performace
			new (targetPtr.as_void) T;
		}
	}
	else
	{
		//Destruct redundant elements
		for (size_t i = newSize; i < m_size; ++i)
		{
			const T* elementToDestruct = &(m_internal_array.as_element[i]);
			//Destructor called by pointer because of possible virtual destructor (polymorphism)
			elementToDestruct->~T();
		}
	}
	m_size = newSize;
}

template <typename T>
void Vector<T>::resize(size_t newSize, const T& object)
{
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
			// introducind a very small overhead to default-initialization but it can be measured and therefore gained us some performace
			new (targetPtr.as_void) T(object);
		}
	}
	else
	{
		//Destruct redundant elements
		for (size_t i = newSize; i < m_size; ++i)
		{
			const T* elementToDestruct = &(m_internal_array.as_element[i]);
			//Destructor called by pointer because of possible virtual destructor (polymorphism)
			elementToDestruct->~T();
		}
	}
	m_size = newSize;
}

template <typename T>
void Vector<T>::reserve(size_t newCapacity)
{
	//If already big enough, do nothing
	if (newCapacity < m_capacity)
	{
		return;
	}

	const size_t growSizeInBytes = (newCapacity - m_capacity) * sizeof(T);
	GrowByBytes(growSizeInBytes);
}

// TODO: Discuss return value
template <typename T>
void Vector<T>::erase(size_t index)
{
	{
		//Check if index is in Range, no negative check needed because unsigned
		const bool isIndexInRange = index < m_size;
		assert("Index out of Range!" && isIndexInRange);
	}

	for (size_t i = index; i < m_size - 1; ++i)
	{
		PointerType current, next;
		current.as_element = &(m_internal_array.as_element[i]);
		next.as_element = &(m_internal_array.as_element[i + 1]);

		// Assign the next to the current element (assuming the user implemented the asignment operator properly)
		// Also a requirement of std::vector (MoveAssignment) implemented
		*current.as_element = *next.as_element;
	}

	// At the end call the dtor for the last element to free its resources too
	PointerType toDestruct;
	toDestruct.as_element = &(m_internal_array.as_element[m_size - 1]);
	toDestruct.as_element->~T();

	--m_size;
}

template <typename T>
void Vector<T>::erase(size_t rangeBegin, size_t rangeEnd)
{
	{
		//With this check, rangeEnd can never equal rangeBegin!
		const bool isEndBiggerThanOrEqualToStart = rangeEnd >= rangeBegin;
		assert("endIndex needs to be larger than startIndex!" && isEndBiggerThanOrEqualToStart);
	}
	
	//	Quote: The iterator first does not need to be dereferenceable if first==last: erasing an empty range is a no-op.
	// Comes from erasing ranges with iterator begin() and end()
	// If begin == end means begin is not dereferencable and can not be deleted -> no-op
	if(rangeEnd == rangeBegin)
	{
		return;
	}

	// Erasing a range needs to bubbling up a group of holes
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

	// Now delete the bubbled up elements that would lack resources if the dtor was not called
	for (size_t i = m_size - elementsToDelete; i < m_size; ++i)
	{
		const T* toDestruct = &(m_internal_array.as_element[i]);
		toDestruct->~T();
	}

	m_size -= elementsToDelete;
}

template <typename T>
void Vector<T>::erase_by_swap(size_t index)
{
	assert("Index out of Range!" && index < m_size);

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

template <typename T>
void Vector<T>::GrowByBytes(size_t growSizeInBytes)
{
	if (growSizeInBytes == 0u) return; // I'm not sure why we ever want to allow a grow by 0
	
	size_t roundedGrowSize = MathUtil::roundUpToMultiple(growSizeInBytes, m_pageSize);

	{
		const bool isEndOfVirtualMemoryReached = m_physical_mem_end.as_ptr == m_virtual_mem_end.as_ptr;
		assert("Maximum Capacity reached! Vector can not grow further." && !isEndOfVirtualMemoryReached);
	}

	// If the requested growSizeInBytes would exceed the maximum capacity, grow only to the maximum capacity
	if (m_physical_mem_begin.as_ptr + roundedGrowSize > m_virtual_mem_end.as_ptr)
	{
		size_t maxAvailableGrowSize = m_virtual_mem_end.as_ptr - m_physical_mem_end.as_ptr;
		// We allow the vector to 'overshoot' its max size by a maximum of one page
		roundedGrowSize = MathUtil::roundUpToMultiple(maxAvailableGrowSize, m_pageSize);
	}

	PointerType allocation;
	allocation.as_void = VirtualMemory::GetPhysicalMemory(m_physical_mem_end.as_void, roundedGrowSize);
	m_physical_mem_end.as_ptr = allocation.as_ptr + roundedGrowSize;
	m_capacity = (m_physical_mem_end.as_ptr - m_physical_mem_begin.as_ptr) / sizeof(T);
}

template <typename T>
size_t Vector<T>::GetGrowSizeInElements() const
{
	// This is a small trick we found in a blog and thought about a bit
	// If we allocate one element it is very probable that we allocate a few more and 
	// it shows a small performance gain when allocating 8 slots at the bgeinning instead of going 1-2-4-8 for the first few push_backs
	return m_capacity ? m_capacity * 2 : 8;
}

// ~~~~~~~ Test Program ~~~~~~~
namespace Testing
{
	struct HugeType
	{
		uint32_t data[16384] = { 0xDEADBEEF };
	};

	class TestClass
	{

	public:
		TestClass();
		TestClass(const TestClass& other);
		~TestClass();

		static const size_t m_testValue = 0xDEADBEEFDEADBEEF;

		size_t* m_testArray;
		size_t m_elementCount;
	};

	TestClass::TestClass()
		: m_elementCount(10u)
	{
		m_testArray = new size_t[m_elementCount];

		for (size_t i = 0u; i < m_elementCount; ++i)
		{
			m_testArray[i] = m_testValue;
		}
	}

	TestClass::TestClass(const TestClass & other)
		: m_elementCount(other.m_elementCount)
	{
		m_testArray = new size_t[m_elementCount];

		for (size_t i = 0u; i < m_elementCount; ++i)
		{
			m_testArray[i] = other.m_testArray[i];
		}
	}

	TestClass::~TestClass()
	{
		delete[] m_testArray;
	}

	void TestBasicTypePushBack(size_t count)
	{
		Vector<size_t> testVector;

		for (size_t i = 0u; i < count; ++i)
		{
			testVector.push_back(i);
		}

		for (size_t i = 0u; i < count; ++i)
		{
			const bool indexEqualsValue = testVector[i] == i;
			assert("Could not verify values in Vector!" && indexEqualsValue);
		}

		printf("TestBasicTypePushBack with count %llu done!\n", count);
	}

	void TestBasicClassPushBack(size_t count)
	{
		Vector<TestClass> testVector;

		for (size_t i = 0u; i < count; ++i)
		{
			testVector.push_back(TestClass());
		}

		for (size_t i = 0u; i < count; ++i)
		{
			for (size_t x = 0u; x < testVector[i].m_elementCount; ++x)
			{
				const bool isArrayValueCorrect = testVector[i].m_testArray[x] == TestClass::m_testValue;
				assert("Could not verify values in Vector!" && isArrayValueCorrect);
			}
		}

		printf("TestBasicClassPushBack with count %llu done!\n", count);
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
		assert(testVector.size() == 3u);

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
		assert(testVector.size() == 3u);

		printf("Erase By Swap Test done!\n");
	}

	void TestEraseByRange()
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

		printf("Erase By Range Test done!\n");
	}

	void TestCopyConstructor()
	{
		Vector<size_t> firstVector;

		firstVector.push_back(123u);
		firstVector.push_back(456u);
		firstVector.push_back(789u);
		firstVector.push_back(123456789u);

		Vector<size_t> testVector(firstVector);

		assert(testVector[0] == 123u);
		assert(testVector[1] == 456u);
		assert(testVector[2] == 789u);
		assert(testVector[3] == 123456789u);

		printf("Copy Constructor Test done!\n");
	}

	void TestResizing()
	{
		Vector<size_t> testVector;
		testVector.resize(2500, 0xDEADBEEFu);

		assert(testVector.size() == 2500);
		const size_t capacity = testVector.capacity();

		testVector.resize(500);

		assert(testVector.size() == 500);
		assert(testVector.capacity() == capacity);

		printf("Resizing Test done!\n");
	}

	void TestReserving()
	{
		Vector<size_t> testVector;
		testVector.reserve(2500);

		SYSTEM_INFO sys_inf;
		GetSystemInfo(&sys_inf);

		const size_t pageSize = sys_inf.dwPageSize;

		assert(testVector.empty());
		const size_t expectedSize = (MathUtil::roundUpToMultiple(2500 * sizeof(size_t), pageSize)) / sizeof(size_t);
		assert(testVector.capacity() == expectedSize);

		printf("Reserving Test done!\n");
	}
}

//TODO Make better Tests
//TODO Test with non-default constructors
//TODO Test with Polymorphism
int main()
{
	Testing::TestBasicTypePushBack(100000);
	Testing::TestBasicClassPushBack(100);

	//TestSubscript(-1);
	//TestSubscript(0);

	Testing::TestErase();
	Testing::TestEraseBySwap();
	Testing::TestEraseByRange();

	Testing::TestResizing();
	Testing::TestReserving();

	Testing::TestCopyConstructor();

	printf("All Tests done!\n");
}

