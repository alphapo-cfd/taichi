#pragma once

template <typename T, typename G>
class lock_guard {
 public:
  lock_guard(Ptr lock, const T &func, const G &test) {
#if ARCH_x64 || ARCH_arm64
    mutex_lock_i32(lock);
    func();
    mutex_unlock_i32(lock);
#else
    // CUDA

    auto fast = false;
    if (fast) {
      auto active_mask = cuda_active_mask();
      auto remaining = active_mask;
      while (remaining) {
        auto leader = cttz_i32(remaining);
        if (warp_idx() == leader) {
          // Memory fences here are necessary since CUDA has a weakly ordered
          // memory model across threads
          mutex_lock_i32(lock);
          grid_memfence();
          func();
          grid_memfence();
          mutex_unlock_i32(lock);
        }
        remaining &= ~(1u << leader);
      }
    } else {
      for (int i = 0; i < warp_size(); i++) {
        if (warp_idx() == i) {
          // Memory fences here are necessary since CUDA has a weakly ordered
          // memory model across threads
          if (test()) {
            mutex_lock_i32(lock);
            grid_memfence();
            if (test()) {
              func();
            }
            grid_memfence();
            mutex_unlock_i32(lock);
          }
        }
      }
    }
    // Unfortunately critical sections on CUDA has undefined behavior (deadlock
    // or not), if more than one thread in a warp try to acquire locks
    /*
    bool done = false;
    while (!done) {
      if (atomic_exchange_i32((i32 *)lock, 1) == 1) {
        func();
        done = true;
        mutex_unlock_i32(lock);
      }
    }
    */
#endif
  }
};

template <typename T, typename G>
void locked_task(void *lock, const T &func, const G &test) {
  lock_guard<T, G> _((Ptr)lock, func, test);
}

template <typename T>
void locked_task(void *lock, const T &func) {
  locked_task(lock, func, []() { return true; });
}
