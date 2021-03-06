#include <gtest/gtest.h>
#include <memory>
#include <iv/detail/cstdint.h>
#include <iv/alloc.h>

TEST(SpaceCase, AllocateMemoryAlignemntTest) {
  std::unique_ptr<iv::core::Space> space(new iv::core::Space());
  {
    uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(3));
    ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
  }
  {
    uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(3));
    ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
  }
  {
    uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(3));
    ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
  }
  {
    uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(1000));
    ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
  }
  {
    uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(100));
    ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
  }
  {
    for (std::size_t i = 0; i < 1000000; ++i) {
      uintptr_t mem = reinterpret_cast<uintptr_t>(space->New(23));
      ASSERT_EQ(0u, mem % iv::core::Arena::kAlignment);
    }
  }
}
