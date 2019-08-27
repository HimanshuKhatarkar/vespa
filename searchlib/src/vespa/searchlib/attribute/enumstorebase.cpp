// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "enumstorebase.h"
#include "enumstore.h"
#include "enum_store_dictionary.h"
#include <vespa/vespalib/btree/btree.hpp>
#include <vespa/vespalib/btree/btreeiterator.hpp>
#include <vespa/vespalib/btree/btreenode.hpp>
#include <vespa/vespalib/btree/btreenodeallocator.hpp>
#include <vespa/vespalib/btree/btreeroot.hpp>
#include <vespa/vespalib/datastore/datastore.hpp>
#include <vespa/vespalib/datastore/unique_store_dictionary.hpp>
#include <vespa/vespalib/stllike/asciistream.h>
#include <vespa/vespalib/stllike/hash_map.hpp>
#include <vespa/vespalib/util/bufferwriter.h>
#include <vespa/vespalib/util/exceptions.h>
#include <vespa/vespalib/util/rcuvector.hpp>


#include <vespa/log/log.h>
LOG_SETUP(".searchlib.attribute.enumstorebase");

namespace search {

using btree::BTreeNode;

EnumStoreBase::EnumBufferType::EnumBufferType()
    : datastore::BufferType<char>(Index::align(1),
                                  Index::offsetSize() / Index::align(1),
                                  Index::offsetSize() / Index::align(1)),
      _minSizeNeeded(0),
      _deadElems(0),
      _pendingCompact(false),
      _wantCompact(false)
{
}

size_t
EnumStoreBase::EnumBufferType::calcArraysToAlloc(uint32_t bufferId, size_t sizeNeeded, bool resizing) const
{
    (void) resizing;
    size_t reservedElements = getReservedElements(bufferId);
    sizeNeeded = std::max(sizeNeeded, _minSizeNeeded);
    size_t usedElems = _activeUsedElems;
    if (_lastUsedElems != nullptr) {
        usedElems += *_lastUsedElems;
    }
    assert((usedElems % _arraySize) == 0);
    double growRatio = 1.5f;
    uint64_t maxSize = static_cast<uint64_t>(_maxArrays) * _arraySize;
    uint64_t newSize = usedElems - _deadElems + sizeNeeded;
    if (usedElems != 0) {
        newSize *= growRatio;
    }
    newSize += reservedElements;
    newSize = alignBufferSize(newSize);
    assert((newSize % _arraySize) == 0);
    if (newSize <= maxSize) {
        return newSize / _arraySize;
    }
    newSize = usedElems - _deadElems + sizeNeeded + reservedElements + 1000000;
    newSize = alignBufferSize(newSize);
    assert((newSize % _arraySize) == 0);
    if (newSize <= maxSize) {
        return _maxArrays;
    }
    failNewSize(newSize, maxSize);
    return 0;
}

EnumStoreBase::EnumStoreBase(uint64_t initBufferSize, bool hasPostings)
    : _enumDict(nullptr),
      _store(),
      _type(),
      _toHoldBuffers()
{
    if (hasPostings)
        _enumDict = new EnumStoreDictionary<EnumPostingTree>(*this);
    else
        _enumDict = new EnumStoreDictionary<EnumTree>(*this);
    _store.addType(&_type);
    _type.setSizeNeededAndDead(initBufferSize, 0);
    _store.initActiveBuffers();
}

EnumStoreBase::~EnumStoreBase()
{
    _store.clearHoldLists();
    _store.dropBuffers();
    delete _enumDict;
}

void
EnumStoreBase::reset(uint64_t initBufferSize)
{
    _store.clearHoldLists();
    _store.dropBuffers();
    _type.setSizeNeededAndDead(initBufferSize, 0);
    _store.initActiveBuffers();
    _enumDict->onReset();
}

uint32_t
EnumStoreBase::getBufferIndex(datastore::BufferState::State status)
{
    for (uint32_t i = 0; i < _store.getNumBuffers(); ++i) {
        if (_store.getBufferState(i).getState() == status) {
            return i;
        }
    }
    return Index::numBuffers();
}

vespalib::MemoryUsage
EnumStoreBase::getMemoryUsage() const
{
    return _store.getMemoryUsage();
}

vespalib::AddressSpace
EnumStoreBase::getAddressSpaceUsage() const
{
    const datastore::BufferState &activeState = _store.getBufferState(_store.getActiveBufferId(TYPE_ID));
    return vespalib::AddressSpace(activeState.size(), activeState.getDeadElems(), DataStoreType::RefType::offsetSize());
}

void
EnumStoreBase::transferHoldLists(generation_t generation)
{
    _enumDict->transfer_hold_lists(generation);
    _store.transferHoldLists(generation);
}

void
EnumStoreBase::trimHoldLists(generation_t firstUsed)
{
    // remove generations in the range [0, firstUsed>
    _enumDict->trim_hold_lists(firstUsed);
    _store.trimHoldLists(firstUsed);
}

bool
EnumStoreBase::preCompact(uint64_t bytesNeeded)
{
    if (getBufferIndex(datastore::BufferState::FREE) == Index::numBuffers()) {
        return false;
    }
    uint32_t activeBufId = _store.getActiveBufferId(TYPE_ID);
    datastore::BufferState & activeBuf = _store.getBufferState(activeBufId);
    _type.setSizeNeededAndDead(bytesNeeded, activeBuf.getDeadElems());
    _toHoldBuffers = _store.startCompact(TYPE_ID);
    return true;
}


void
EnumStoreBase::fallbackResize(uint64_t bytesNeeded)
{
    uint32_t activeBufId = _store.getActiveBufferId(TYPE_ID);
    size_t reservedElements = _type.getReservedElements(activeBufId);
    _type.setSizeNeededAndDead(bytesNeeded, reservedElements);
    _type.setWantCompact();
    _store.fallbackResize(activeBufId, bytesNeeded);
}


void
EnumStoreBase::postCompact()
{
    _store.finishCompact(_toHoldBuffers);
}

void
EnumStoreBase::failNewSize(uint64_t minNewSize, uint64_t maxSize)
{
    throw vespalib::IllegalStateException(vespalib::make_string("EnumStoreBase::failNewSize: Minimum new size (%" PRIu64 ") exceeds max size (%" PRIu64 ")", minNewSize, maxSize));
}

ssize_t
EnumStoreBase::deserialize0(const void *src,
                                size_t available,
                                IndexVector &idx)
{
    size_t left = available;
    size_t initSpace = Index::align(1);
    const char * p = static_cast<const char *>(src);
    while (left > 0) {
        ssize_t sz = deserialize(p, left, initSpace);
        if (sz < 0)
            return sz;
        p += sz;
        left -= sz;
    }
    reset(initSpace);
    left = available;
    p = static_cast<const char *>(src);
    Index idx1;
    while (left > 0) {
        ssize_t sz = deserialize(p, left, idx1);
        if (sz < 0)
            return sz;
        p += sz;
        left -= sz;
        idx.push_back(idx1);
    }
    return available - left;
}


template <typename Tree>
ssize_t
EnumStoreBase::deserialize(const void *src,
                               size_t available,
                               IndexVector &idx,
                               Tree &tree)
{
    ssize_t sz(deserialize0(src, available, idx));
    if (sz >= 0) {
        typename Tree::Builder builder(tree.getAllocator());
        typedef IndexVector::const_iterator IT;
        for (IT i(idx.begin()), ie(idx.end()); i != ie; ++i) {
            builder.insert(*i, typename Tree::DataType());
        }
        tree.assign(builder);
    }
    return sz;
}


template <typename Tree>
void
EnumStoreBase::fixupRefCounts(const EnumVector &hist, Tree &tree)
{
    if ( hist.empty() )
        return;
    typename Tree::Iterator ti(tree.begin());
    typedef EnumVector::const_iterator HistIT;

    for (HistIT hi(hist.begin()), hie(hist.end()); hi != hie; ++hi, ++ti) {
        assert(ti.valid());
        fixupRefCount(ti.getKey(), *hi);
    }
    assert(!ti.valid());
    freeUnusedEnums(false);
}


vespalib::asciistream & operator << (vespalib::asciistream & os, const EnumStoreBase::Index & idx) {
    return os << "offset(" << idx.offset() << "), bufferId(" << idx.bufferId() << "), idx(" << idx.ref() << ")";
}


template class datastore::DataStoreT<EnumStoreIndex>;

template
ssize_t
EnumStoreBase::deserialize<EnumTree>(const void *src, size_t available, IndexVector &idx, EnumTree &tree);

template
ssize_t
EnumStoreBase::deserialize<EnumPostingTree>(const void *src, size_t available, IndexVector &idx, EnumPostingTree &tree);

template
void
EnumStoreBase::fixupRefCounts<EnumTree>(const EnumVector &hist, EnumTree &tree);

template
void
EnumStoreBase::fixupRefCounts<EnumPostingTree>(const EnumVector &hist, EnumPostingTree &tree);

}

namespace vespalib {
template class RcuVectorBase<search::EnumStoreIndex>;
}

VESPALIB_HASH_MAP_INSTANTIATE_H_E_M(search::EnumStoreIndex, search::EnumStoreIndex,
                                    vespalib::hash<search::EnumStoreIndex>, std::equal_to<search::EnumStoreIndex>,
                                    vespalib::hashtable_base::and_modulator);
