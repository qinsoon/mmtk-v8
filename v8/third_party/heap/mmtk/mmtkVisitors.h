#ifndef MMTK_VISITORS_H
#define MMTK_VISITORS_H

#include "src/base/logging.h"
#include "mmtkUpcalls.h"
#include <mutex>
#include <condition_variable>
#include "src/objects/slots-inl.h"
#include "src/heap/safepoint.h"
#include "src/codegen/reloc-info.h"
#include "src/objects/code.h"
#include "src/objects/code-inl.h"
#include "src/codegen/assembler-inl.h"
#include <unordered_set>


namespace v8 {
namespace internal {
namespace third_party_heap {


class MMTkEdgeVisitor: public RootVisitor, public ObjectVisitor {
 public:
  explicit MMTkEdgeVisitor(ProcessEdgesFn process_edges): process_edges_(process_edges) {
    NewBuffer buf = process_edges(NULL, 0, 0);
    buffer_ = buf.buf;
    cap_ = buf.cap;
  }

  virtual ~MMTkEdgeVisitor() {
    if (cursor_ > 0) flush();
    if (buffer_ != NULL) {
      release_buffer(buffer_, cursor_, cap_);
    }
  }

  virtual void VisitRootPointer(Root root, const char* description, FullObjectSlot p) override final {
    ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) override final {
    for (FullObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) override final {
    for (OffHeapObjectSlot p = start; p < end; ++p) ProcessRootEdge(root, p);
  }

  virtual void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override final {
    for (ObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override final {
    for (MaybeObjectSlot p = start; p < end; ++p) ProcessEdge(p);
  }

  virtual void VisitCodeTarget(Code host, RelocInfo* rinfo) override final {
    // We need to pass the location of the root pointer (i.e. the slot) to MMTk. Fake it for now.
    Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    auto p = new HeapObject(target);
    buffer_[cursor_++] = (void*) p;
    if (cursor_ >= cap_) {
      flush();
    }
  }

  virtual void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) override final {
    // We need to pass the location of the root pointer (i.e. the slot) to MMTk. Fake it for now.
    auto p = new HeapObject(rinfo->target_object());
    buffer_[cursor_++] = (void*) p;
    if (cursor_ >= cap_) {
      flush();
    }
  }

  virtual void VisitMapPointer(HeapObject host) override final {
    ProcessEdge(host.map_slot());
  }

 private:
  V8_INLINE void ProcessRootEdge(Root root, FullObjectSlot p) {
    ProcessEdge(p);
  }

  template<class T>
  V8_INLINE void ProcessEdge(T p) {
    HeapObject object;
    if ((*p).GetHeapObject(&object)) {
        auto s = new HeapObject(object);

        buffer_[cursor_++] = (void*) s;
        if (cursor_ >= cap_) {
          flush();
        }
    }
  }

  void flush() {
    if (cursor_ > 0) {
      NewBuffer buf = process_edges_(buffer_, cursor_, cap_);
      buffer_ = buf.buf;
      cap_ = buf.cap;
      cursor_ = 0;
    }
  }

  ProcessEdgesFn process_edges_;
  void** buffer_ = nullptr;
  size_t cap_ = 0;
  size_t cursor_ = 0;
};



class MMTkHeapVerifier: public RootVisitor, public ObjectVisitor {
 public:
  explicit MMTkHeapVerifier() {}

  virtual ~MMTkHeapVerifier() {}

  virtual void VisitRootPointer(Root root, const char* description, FullObjectSlot p) override final {
    VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, FullObjectSlot start, FullObjectSlot end) override final {
    for (FullObjectSlot p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitRootPointers(Root root, const char* description, OffHeapObjectSlot start, OffHeapObjectSlot end) override final {
    for (OffHeapObjectSlot p = start; p < end; ++p) VerifyRootEdge(root, p);
  }

  virtual void VisitPointers(HeapObject host, ObjectSlot start, ObjectSlot end) override final {
    for (ObjectSlot p = start; p < end; ++p) VerifyEdge(p);
  }

  virtual void VisitPointers(HeapObject host, MaybeObjectSlot start, MaybeObjectSlot end) override final {
    for (MaybeObjectSlot p = start; p < end; ++p) VerifyEdge(p);
  }

  virtual void VisitCodeTarget(Code host, RelocInfo* rinfo) override final {
    Code target = Code::GetCodeFromTargetAddress(rinfo->target_address());
    VerifyHeapObject(0, target);
  }

  virtual void VisitEmbeddedPointer(Code host, RelocInfo* rinfo) override final {
    VerifyHeapObject(0, rinfo->target_object());
  }

  virtual void VisitMapPointer(HeapObject host) override final {
    VerifyEdge(host.map_slot());
  }

 private:
  V8_INLINE void VerifyRootEdge(Root root, FullObjectSlot p) {
    VerifyEdge(p);
  }

  template<class T>
  V8_INLINE void VerifyEdge(T p) {
    HeapObject object;
    if ((*p).GetHeapObject(&object)) {
        VerifyHeapObject(p.address(), object);
    }
  }

  V8_INLINE void VerifyHeapObject(Address edge, HeapObject o) {
    if (marked_objects_.find(o.ptr()) == marked_objects_.end()) {
        marked_objects_.insert(o.ptr());
        if (!third_party_heap::Heap::IsValidHeapObject(o)) {
            printf("Dead edge %p -> %p\n", (void*) edge, (void*) o.ptr());
        }
        CHECK(third_party_heap::Heap::IsValidHeapObject(o));
        o.Iterate(this);
    }
  }

  std::unordered_set<Address> marked_objects_;
};




}
}
}


#endif // MMTK_VISITORS_H