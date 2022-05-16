#include <gtest/gtest.h>

#include "General.h"

#include "Common.h"
#include "Database.h"
#include "Logger.h"

namespace PapierMache::DbStuff {

    class IdGeneratorTest : public ::testing::Test {
    protected:
        IdGeneratorTest()
        {
        }

        ~IdGeneratorTest() override
        {
        }

        void SetUp() override
        {
        }

        void TearDown() override
        {
        }
    };

    TEST_F(IdGeneratorTest, test001)
    {
        IdGenerator<short> idg{0};
        ASSERT_EQ(0, idg.getId());
        ASSERT_EQ(1, idg.getId());
        ASSERT_EQ(2, idg.getId());
    }

    TEST_F(IdGeneratorTest, test002)
    {
        IdGenerator<short> idg{10};
        ASSERT_EQ(10, idg.getId());
        ASSERT_EQ(11, idg.getId());
        ASSERT_EQ(12, idg.getId());
    }

} // namespace PapierMache::DbStuff
