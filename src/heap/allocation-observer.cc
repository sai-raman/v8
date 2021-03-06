// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/allocation-observer.h"

#include "src/heap/heap.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

void AllocationCounter::AddAllocationObserver(AllocationObserver* observer) {
  allocation_observers_.push_back(observer);
}

void AllocationCounter::RemoveAllocationObserver(AllocationObserver* observer) {
  auto it = std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer);
  DCHECK(allocation_observers_.end() != it);
  allocation_observers_.erase(it);
}

intptr_t AllocationCounter::GetNextInlineAllocationStepSize() {
  intptr_t next_step = 0;
  for (AllocationObserver* observer : allocation_observers_) {
    next_step = next_step ? Min(next_step, observer->bytes_to_next_step())
                          : observer->bytes_to_next_step();
  }
  DCHECK(!HasAllocationObservers() || next_step > 0);
  return next_step;
}

void AllocationCounter::NotifyBytes(size_t allocated) {
  if (!IsActive()) {
    return;
  }

  DCHECK_LE(allocated, next_counter_ - current_counter_);
  current_counter_ += allocated;
}

void AllocationCounter::NotifyObject(Address soon_object, size_t object_size) {
  if (!IsActive()) {
    return;
  }

  DCHECK_GT(object_size, next_counter_ - current_counter_);
  size_t bytes_since_last_step = current_counter_ - prev_counter_;
  DCHECK(!heap_->allocation_step_in_progress());
  heap_->set_allocation_step_in_progress(true);
  DCHECK(soon_object);
  heap_->CreateFillerObjectAt(soon_object, static_cast<int>(object_size),
                              ClearRecordedSlots::kNo);
  intptr_t next_step = 0;
  for (AllocationObserver* observer : allocation_observers_) {
    observer->AllocationStep(static_cast<int>(bytes_since_last_step),
                             soon_object, object_size);
    next_step = next_step ? Min(next_step, observer->bytes_to_next_step())
                          : observer->bytes_to_next_step();
  }
  heap_->set_allocation_step_in_progress(false);

  prev_counter_ = current_counter_;
  next_counter_ = current_counter_ + object_size + next_step;
}

void AllocationObserver::AllocationStep(int bytes_allocated,
                                        Address soon_object, size_t size) {
  DCHECK_GE(bytes_allocated, 0);
  bytes_to_next_step_ -= bytes_allocated;
  if (bytes_to_next_step_ <= 0) {
    Step(static_cast<int>(step_size_ - bytes_to_next_step_), soon_object, size);
    step_size_ = GetNextStepSize();
    bytes_to_next_step_ = step_size_;
  }
  DCHECK_GE(bytes_to_next_step_, 0);
}

PauseAllocationObserversScope::PauseAllocationObserversScope(Heap* heap)
    : heap_(heap) {
  DCHECK_EQ(heap->gc_state(), Heap::NOT_IN_GC);

  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->PauseAllocationObservers();
  }
}

PauseAllocationObserversScope::~PauseAllocationObserversScope() {
  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->ResumeAllocationObservers();
  }
}

}  // namespace internal
}  // namespace v8
