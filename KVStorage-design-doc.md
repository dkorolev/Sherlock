
# KVStorage Design Doc

**KVStorage** is a layer of abstraction above **Sherlock** stream, which implements a key-value storage functionality. It is responsible for keeping in-memory state and data consistency. 

KVStorage provides the API to **Create**, **Read**, **Update** and **Delete** records (**CRUD**).

One can think of KVStorage as a table-style *materialistic view* of the data stored consequtively in a Sherlock stream.

## Data Structure

A type should meet certain specificaitons in order to be supported by KVStorage.

Required specifications *(for **Set/Add**() and **Get()**)*:

1. Serializable itself (*"**T_ENTRY**"* is *"cerealizable"*.)
1. Have a defined type for the key (*"typename **T_ENTRY::T_KEY**" is defined.*)
1. Have the means to extract the key (*ex. **T_KEY GetKey()) const;** .*)

Other specifications (**TODO(mzhurovich+dkorolev): Define these.**):

1. Have the means to declare a record as empty and to test for it (for DELETE).
1. Have the means to compose KEY from a string (for REST / HTTP access).
1. Have the means to compose a "NOT FOUND" entry (for getters to be non-throwing by default).
1. Etc ...


### Key

Key structure should implement: 
* `HashFunction` to provide single unique identifier (type?)
* Comparison operator `==`

## API Structure

There are three groups of access methods:

* **Get** methods
* **Set** methods
* **Delete** methods

For each method there are three types of implementation:

* Synchronous.
* Asynchronous via `std::future<>`
* Asynchronous with `std::function<>` callbacks.

## Get

### Synchronous version

```cpp
template<typename T_ENTRY, T_NOT_FOUND_POLICY policy = T_NOT_FOUND_POLICY::ThrowIfNotFound> T_ENTRY Get(const typename T_ENTRY::T_KEY& key);
```

If key does not exist, throws an exception. (TODO(mzhurovich+dkorolev): Tweak it.)

### Asynchronous version

```cpp
template<typename T_ENTRY> std::future<T_ENTRY> AsyncGet(const typename T_ENTRY::T_KEY& key);
```

If key does not exist, throws an exception.

### Callback version

```cpp
template<typename T_ENTRY> void AsyncGet(const typename T_ENTRY::T_KEY& key,
                                         std::function<void(const T_ENTRY&)> on_found,
                                         std::function<void(const typename T_ENTRY::T_KEY&)> on_not_found);
```


## Set

All Set methods create new record if key does not exist and modify existing record otherwise.

(TODO(mzhurovich+dkorolev): Tweak it.)

```cpp
template<typename T_ENTRY> void Add(const T_ENTRY& entry);  // Extracts the key from the entry.
template<typename T_ENTRY> void Set(const typename T_ENTRY::T_KEY& key, const T_ENTRY& entry);  // TODO(mzhurovich+dkorolev): Discuss. Should this form only be allowed for the types that support `SetKey()` or something?

template<typename T_ENTRY> std::future<void> AsyncAdd(...);  // TODO(mzhurovich+dkorolev): Exception policy?
template<typename T_ENTRY> std::future<void> AsyncSet(...);
```


## Delete

## Multiple types support

## Data consistency
