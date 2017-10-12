/*
 * CustomVector class as std::vector replacement
 * Authors: Alexander Müller, Stefan Reinhold, Lukas Vogl
 */


template <class T>
class Vector
{

public:
	// Get some initial memory (maybe ?)
	Vector();

	// Return the current size of the vector (element count)
	size_t size(void) const;
	// Return the allocated size if the vector
	size_t capacity(void) const;

	// Push back the element into the vector
	// If capacity is still usable copy the content of object to the next free slot
	// If capacity is depleted, regrow the internal storage by a efficient pattern (std::vector does * 2)
	void push_back(const T& object);
	// Resize the internal storage of the vector 
	// * if n < el_size:		shrink to fit n (and destruct all elements on the way)
	// * if n > el_size:		assign values and construct
	// * if n > allocatiosize:	reallocate more mem till n is reached
	void resize(size_t n, const T& object);
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

	// 
	T* operator[] (size_t index);

	// At this point we have to call the dtors of the elements stored in the vector
	// and release the capacity bytes memory used
	~Vector();

private:
	T* m_internal_array;
	size_t m_size;
	size_t m_capacity;
};

template <class T>
Vector<T>::Vector()
{
}

template <class T>
size_t Vector<T>::size() const
{
}

template <class T>
size_t Vector<T>::capacity() const
{
}

template <class T>
void Vector<T>::push_back(const T& object)
{
}

template <class T>
void Vector<T>::resize(size_t n, const T& object)
{
}

template <class T>
void Vector<T>::reserve(size_t n)
{
}

template <class T>
void Vector<T>::erase(size_t index)
{
}

template <class T>
void Vector<T>::erase_by_swap(size_t index)
{
}

template <class T>
T* Vector<T>::operator[](size_t index)
{
}

template <class T>
Vector<T>::~Vector()
{
}

int main ()
{
	
}
