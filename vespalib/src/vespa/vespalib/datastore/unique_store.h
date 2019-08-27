// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "buffer_type.h"
#include "bufferstate.h"
#include "datastore.h"
#include "entry_comparator_wrapper.h"
#include "entryref.h"
#include "i_compaction_context.h"
#include "i_unique_store_dictionary.h"
#include "unique_store_add_result.h"
#include "unique_store_allocator.h"
#include "unique_store_comparator.h"
#include "unique_store_entry.h"

namespace search::datastore {

template <typename Allocator>
class UniqueStoreBuilder;

template <typename RefT>
class UniqueStoreEnumerator;

/**
 * Datastore for unique values of type EntryT that is accessed via a
 * 32-bit EntryRef.
 */
template <typename EntryT, typename RefT = EntryRefT<22>, typename Compare = UniqueStoreComparator<EntryT, RefT>, typename Allocator = UniqueStoreAllocator<EntryT, RefT> >
class UniqueStore
{
public:
    using DataStoreType = DataStoreT<RefT>;
    using EntryType = EntryT;
    using RefType = RefT;
    using Enumerator = UniqueStoreEnumerator<RefT>;
    using Builder = UniqueStoreBuilder<Allocator>;
    using EntryConstRefType = typename Allocator::EntryConstRefType;
private:
    Allocator _allocator;
    DataStoreType &_store;
    std::unique_ptr<IUniqueStoreDictionary> _dict;
    using generation_t = vespalib::GenerationHandler::generation_t;

public:
    UniqueStore();
    ~UniqueStore();
    UniqueStoreAddResult add(EntryConstRefType value);
    EntryRef find(EntryConstRefType value);
    EntryConstRefType get(EntryRef ref) const { return _allocator.get(ref); }
    void remove(EntryRef ref);
    ICompactionContext::UP compactWorst();
    vespalib::MemoryUsage getMemoryUsage() const;

    // Pass on hold list management to underlying store
    void transferHoldLists(generation_t generation);
    void trimHoldLists(generation_t firstUsed);
    vespalib::GenerationHolder &getGenerationHolder() { return _store.getGenerationHolder(); }
    void setInitializing(bool initializing) { _store.setInitializing(initializing); }
    void freeze();
    uint32_t getNumUniques() const;

    Builder getBuilder(uint32_t uniqueValuesHint);
    Enumerator getEnumerator() const;

    // Should only be used for unit testing
    const BufferState &bufferState(EntryRef ref) const;
};

}
