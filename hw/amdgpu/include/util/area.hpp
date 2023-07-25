#pragma once

#include <cassert>
#include <cstdint>
#include <map>
#include <set>

namespace util {
struct AreaInfo {
  std::uint64_t beginAddress;
  std::uint64_t endAddress;
};

struct NoInvalidationHandle {
  void handleInvalidation(std::uint64_t) {}
};

struct StdSetInvalidationHandle {
  std::set<std::uint64_t, std::greater<>> invalidated;

  void handleInvalidation(std::uint64_t address) {
    invalidated.insert(address);
  }
};

template <typename InvalidationHandleT = NoInvalidationHandle>
class MemoryAreaTable : public InvalidationHandleT {
  enum class Kind { O, X };
  std::map<std::uint64_t, Kind> mAreas;

public:
  class iterator {
    using map_iterator = typename std::map<std::uint64_t, Kind>::iterator;
    map_iterator it;

  public:
    iterator() = default;
    iterator(map_iterator it) : it(it) {}

    AreaInfo operator*() const { return {it->first, std::next(it)->first}; }

    iterator &operator++() {
      ++it;
      ++it;
      return *this;
    }

    iterator &operator--() {
      --it;
      --it;
      return *this;
    }

    bool operator==(iterator other) const { return it == other.it; }
    bool operator!=(iterator other) const { return it != other.it; }
  };

  iterator begin() { return iterator(mAreas.begin()); }
  iterator end() { return iterator(mAreas.end()); }

  void clear() { mAreas.clear(); }

  AreaInfo queryArea(std::uint64_t address) const {
    auto it = mAreas.lower_bound(address);
    assert(it != mAreas.end());
    std::uint64_t endAddress = 0;
    if (it->first != address) {
      assert(it->second == Kind::X);
      endAddress = it->first;
      --it;
    } else {
      assert(it->second == Kind::O);
      endAddress = std::next(it)->first;
    }

    auto startAddress = std::uint64_t(it->first);

    return {startAddress, endAddress};
  }

  void map(std::uint64_t beginAddress, std::uint64_t endAddress) {
    auto [beginIt, beginInserted] = mAreas.emplace(beginAddress, Kind::O);
    auto [endIt, endInserted] = mAreas.emplace(endAddress, Kind::X);

    if (!beginInserted) {
      if (beginIt->second == Kind::X) {
        // it was close, extend to open
        assert(beginIt != mAreas.begin());
        --beginIt;
      }
    } else if (beginIt != mAreas.begin()) {
      auto prevRangePointIt = std::prev(beginIt);

      if (prevRangePointIt->second == Kind::O) {
        // we found range start before inserted one, remove insertion and extend
        // begin
        this->handleInvalidation(beginIt->first);
        mAreas.erase(beginIt);
        beginIt = prevRangePointIt;
      }
    }

    if (!endInserted) {
      if (endIt->second == Kind::O) {
        // it was open, extend to close
        assert(endIt != mAreas.end());
        ++endIt;
      }
    } else {
      auto nextRangePointIt = std::next(endIt);

      if (nextRangePointIt != mAreas.end() &&
          nextRangePointIt->second == Kind::X) {
        // we found range end after inserted one, remove insertion and extend
        // end
        this->handleInvalidation(std::prev(endIt)->first);
        mAreas.erase(endIt);
        endIt = nextRangePointIt;
      }
    }

    // eat everything in middle of the range
    ++beginIt;
    while (beginIt != endIt) {
      this->handleInvalidation(std::prev(endIt)->first);
      beginIt = mAreas.erase(beginIt);
    }
  }

  void unmap(std::uint64_t beginAddress, std::uint64_t endAddress) {
    auto beginIt = mAreas.lower_bound(beginAddress);

    if (beginIt == mAreas.end() || beginIt->first >= endAddress) {
      return;
    }
    if (beginIt->first > beginAddress && beginIt->second == Kind::X) {
      // we have found end after unmap begin, need to insert new end
      this->handleInvalidation(std::prev(beginIt)->first);
      auto newBeginIt = mAreas.emplace_hint(beginIt, beginAddress, Kind::X);
      mAreas.erase(beginIt);

      if (newBeginIt == mAreas.end()) {
        return;
      }

      beginIt = std::next(newBeginIt);
    } else if (beginIt->second == Kind::X) {
      beginIt = ++beginIt;
    }

    Kind lastKind = Kind::X;
    while (beginIt != mAreas.end() && beginIt->first <= endAddress) {
      lastKind = beginIt->second;
      if (lastKind == Kind::O) {
        this->handleInvalidation(std::prev(beginIt)->first);
      }
      beginIt = mAreas.erase(beginIt);
    }

    if (lastKind != Kind::O) {
      return;
    }

    // Last removed was range open, need to insert new one at unmap end
    mAreas.emplace_hint(beginIt, endAddress, Kind::O);
  }

  std::size_t totalMemory() const {
    std::size_t result = 0;

    for (auto it = mAreas.begin(), end = mAreas.end(); it != end; ++it) {
      auto rangeBegin = it;
      auto rangeEnd = ++it;

      result += rangeEnd->first - rangeBegin->first;
    }

    return result;
  }
};

} // namespace util