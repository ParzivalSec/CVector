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
	// Initialize Empty Vector (allocates only address space)
	Vector(void);

	// Make a DEEP Copy of the Vector
	Vector(const Vector& other);

	// At this point we have to call the dtors of the elements stored in the vector
	// and release the capacity bytes memory used
	~Vector(void);

	// Return the current size of the vector (element count)
	size_t size(void) const;

	// Return the capacity of the vector (possible element count)
	size_t capacity(void) const;

	// Returns wether vector is empty or not
	bool empty(void) const;

	// Push back the element into the vector
	// Uses the objects Copy Constructor, since move semantics are forbidden in this exercise
	// If capacity is depleted, grow the internal storage by an efficient value (see GetDefaultGrowSize())
	void push_back(const T& object);

	// Resize the internal storage of the vector 
	// Should not call resize(size_t, const T&) because object would have to be CopyConstructable/CopyInsertable
	// Standard spezifies that this overload does not have to be CopyConstructable/CopyInsertable
	// TODO Check Standard and other implementations
	void resize(size_t newSize);

	// Resize the internal storage of the vector with default object to use for initialization
	// * if newSize < size:			destruct all redundant objects, capacity stays the same (vector does not shrink to fit when resizing)
	// * if newSize > size:			assign values and construct
	// * if newSize > capacity:		grow to ensure memory for newSize number of elements
	// new objects of type T are Copy Constructed in place
	void resize(size_t newSize, const T& object);

	// Reserve memory in the internal storage to fit at least number of newCapacity elements
	// If newCapacity is lesser than the capacity nothing is done
	// Reserve shall not alter the containers element
	void reserve(size_t newCapacity);

	// Erasing one Element
	void erase(size_t index);

	// Erasing range of elements
	void erase(size_t startIndex, size_t endIndex);

	// Erase by swap shall work in O(1) by 
	// * Delete the element at index (dtor)
	// * Copy the last to its place (placement new + cctor)
	// * Delete the element at the copied location (dtor)
	// TODO Currently Done with memmove(), check if we really dont need to reconstruct an intact object.
	// TODO Object just moves place, pointers inside stay intact.  (shallow vs deep copy)
	void erase_by_swap(size_t index);

	// Subscript operator to access element at specific location/index
	// * returns a reference to make it changeable
	T& operator[] (size_t index);

	// Subscript operator to access element at specific location/index
	// * returns a reference to make it changeable
	const T& operator[] (size_t index) const; 

private:
	//Init funciton initializes virtual address space, does not commit memory
	void Init();

	//Grows the internal capacity by the grow size rounded up to the next page size
	//Size is in bytes!
	void Grow(size_t growSize);

	//Returns the default grow size (Current capacity)
	size_t GetDefaultGrowSize() const;

	PointerType m_internal_array;
	size_t m_size;
	size_t m_capacity;

	PointerType m_virtual_memory_begin;
	PointerType m_virtual_memory_end;
	PointerType m_committed_memory_begin;
	PointerType m_committed_memory_end;
	size_t m_pageSize;

	//Maximum vector capacity as mentioned in lecture - 1GB
	//TODO Find out if we want to grow adress space too
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

	Init();
}

template<class T>
void Vector<T>::Init()
{
	// Get a block of 1GB MB virtual address space from the OS 
	m_virtual_memory_begin.as_void = VirtualAlloc(nullptr, MAX_VECTOR_CAPACITY, MEM_RESERVE, PAGE_NOACCESS);
	m_virtual_memory_end.as_ptr = m_virtual_memory_begin.as_ptr + MAX_VECTOR_CAPACITY;

	//Initialize committed memory to be empty
	m_committed_memory_begin = m_virtual_memory_begin;
	m_committed_memory_end = m_committed_memory_begin;

	m_internal_array = m_committed_memory_begin;
}

template<class T>
Vector<T>::Vector(const Vector<T>& other)
	: m_size(0u)
	, m_capacity(0u)
	, m_pageSize(other.m_pageSize)
{
	//TODO Optimization -> If T trivially contructable use memcpy?

	Init();
	reserve(other.m_capacity);

	for(size_t i = 0; i < other.m_size; ++i)
	{
		push_back((other[i]));
	}
}


template <class T>
Vector<T>::~Vector()
{
	//Desctuction of elements
	for(size_t i = 0u; i < m_size; ++i)
	{
		const T* elementToDestruct = &(m_internal_array.as_element[i]);
		//Destructor called by pointer because of possible virtual destructor (polymorphism)
		elementToDestruct->~T();
	}

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

//Using Copy Constructor since move semantics (std::move()) are forbidden for this exercise 
template <class T>
void Vector<T>::push_back(const T& object)
{
	if(m_capacity == m_size)
	{
		//Grow by Default Size
		Grow(GetDefaultGrowSize());
	}

	PointerType targetPtr;
	targetPtr.as_ptr = m_internal_array.as_ptr + m_size * sizeof(T);
	new (targetPtr.as_void) T(object);
	
	++m_size;
}

//This overload 
template <class T>
void Vector<T>::resize(size_t newSize)
{
	//If size already equals newSize -> do nothing
	if (newSize == m_size)
	{
		return;
	}

	//If new size is greater than current size
	if (newSize > m_size)
	{
		if (m_capacity < newSize)
		{
			const size_t growSize = (newSize - m_capacity) * sizeof(T);

			Grow(growSize);
		}

		PointerType targetPtr;
		for (size_t i = m_size; i < newSize; ++i)
		{
			targetPtr.as_ptr = m_internal_array.as_ptr + i * sizeof(T);
			new (targetPtr.as_void) T();
		}
	}
	else //if new Size is smaller than old size
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

template<class T>
void Vector<T>::resize(size_t newSize, const T& object)
{
	//If size already equals newSize -> do nothing
	if (newSize == m_size)
	{
		return;
	}

	//If new size is greater than current size
	if (newSize > m_size)
	{
		if (m_capacity < newSize)
		{
			const size_t growSize = (newSize - m_capacity) * sizeof(T);
	
			Grow(growSize);
		}

		PointerType targetPtr;
		for (size_t i = m_size; i < newSize; ++i)
		{
			targetPtr.as_ptr = m_internal_array.as_ptr + i * sizeof(T);
			new (targetPtr.as_void) T(object);
		}
	}
	else //if new Size is smaller than old size
	{
		//Destruct redundant elements
		for(size_t i = newSize; i < m_size; ++i)
		{
			const T* elementToDestruct = &(m_internal_array.as_element[i]);
			//Destructor called by pointer because of possible virtual destructor (polymorphism)
			elementToDestruct->~T();
		}
	}

	m_size = newSize;
}

template <class T>
void Vector<T>::reserve(size_t newCapacity)
{
	//If already big enough, do nothing
	if(newCapacity < m_capacity)
	{
		return;
	}

	const size_t growSize = (newCapacity - m_capacity) * sizeof(T);
	Grow(growSize);
}

template <class T>
void Vector<T>::erase(size_t index)
{
	PointerType destination;
	destination.as_element = &(m_internal_array.as_element[index]);

	//Destructor called by pointer because of possible virtual destructor (polymorphism)
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

//Quote from CPP Reference: The iterator first does not need to be dereferenceable if first==last: erasing an empty range is a no-op.
//TODO Empty Ranges are a noop? How? Range not knowable at compile time?
template <class T>
void Vector<T>::erase(size_t startIndex, size_t endIndex)
{
	//Rangechecks are always done in [] operator -> So no extra assert needed
	if(startIndex == endIndex)
	{
		erase(endIndex);
		return;
	}

	{
		const bool isEndBiggerThanStart = endIndex > startIndex;
		assert("endIndex needs to be larger than startIndex!" && isEndBiggerThanStart);
	}
	
	PointerType destination;
	destination.as_element = &(m_internal_array.as_element[startIndex]);
	
	//Deconstruct including endIndex
	for(size_t i = startIndex; i <= endIndex; ++i)
	{
		const T* elementToDestruct = &(m_internal_array.as_element[i]);
		//Destructor called by pointer because of possible virtual destructor (polymorphism)
		elementToDestruct->~T();
	}

	//if not last element, close empty slots by moving the memory
	if (endIndex < m_size - 1)
	{
		PointerType source;
		source.as_ptr = m_internal_array.as_ptr + (endIndex + 1) * sizeof(T);
		memmove(destination.as_void, source.as_void, (m_size - 1 - endIndex) * sizeof(T));
	}
}

template <class T>
void Vector<T>::erase_by_swap(size_t index)
{
	PointerType destination;
	destination.as_element = &(m_internal_array.as_element[index]);

	//Destructor called by pointer because of possible virtual destructor (polymorphism)
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
T& Vector<T>::operator[](size_t index)
{
	//No check for >= 0 needed because index is unsigned!
	assert("Subscript out of range!" && index < m_size);
	return m_internal_array.as_element[index];
}

template<class T>
const T & Vector<T>::operator[](size_t index) const
{
	//No check for >= 0 needed because index is unsigned!
	assert("Subscript out of range!" && index < m_size);
	return m_internal_array.as_element[index];
}

template<class T>
void Vector<T>::Grow(size_t growSize)
{
	//Calculate growSize
	if(growSize != 0u)
	{
		growSize = VirtualUnicornStuff::roundUp(growSize, m_pageSize);
	} else
	{
		growSize = VirtualUnicornStuff::roundUp(sizeof(T), m_pageSize);
	}
	
	//Edge Case: Growing from 501MB would end over 1GB but half of vector is unused
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

template<class T>
size_t Vector<T>::GetDefaultGrowSize() const
{
	return m_capacity * sizeof(T);
}

/*
 *	#################### Test Application ####################
 */

namespace Testing
{
	class TestClass
	{
		
	public:
		TestClass();
		TestClass(const TestClass& other);
		~TestClass();

		static const size_t m_testValue = 0xDEADBEEF;

		size_t* m_testArray;
		size_t m_elementCount;
	};

	TestClass::TestClass()
		: m_elementCount(10u)
	{
		m_testArray = new size_t[m_elementCount];

		for(size_t i = 0u; i < m_elementCount; ++i)
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
			for(size_t x = 0u; x < testVector[i].m_elementCount; ++x)
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
		const size_t expectedSize = (VirtualUnicornStuff::roundUp(2500 * sizeof(size_t), pageSize)) / sizeof(size_t);
		assert(testVector.capacity() == expectedSize);

		printf("Reserving Test done!\n");
	}

}


//TODO Make better Tests
//TODO Test with non-default constructors
//TODO Test with Polymorphism
int main ()
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
