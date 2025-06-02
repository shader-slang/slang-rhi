#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#include "core/sha1.h"

TEST_CASE("sha1")
{
    SUBCASE("constructor")
    {
        {
            SHA1 sha1;
            CHECK(sha1.getHexDigest() == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
        }

        {
            std::string data = "hello world";
            SHA1 sha1(data.data(), data.size());
            CHECK(sha1.getHexDigest() == "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
        }

        {
            SHA1 sha1("hello world");
            CHECK(sha1.getHexDigest() == "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
        }
    }
    SUBCASE("copy constructor")
    {
        SHA1 sha1("hello world");
        SHA1 copy(sha1);
        CHECK(copy.getHexDigest() == "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
    }

    SUBCASE("copy assignment")
    {
        SHA1 sha1("hello world");
        SHA1 copy;
        copy = sha1;
        CHECK(copy.getHexDigest() == "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
    }

    SUBCASE("update")
    {
        {
            SHA1 sha1;
            sha1.update('h');
            sha1.update('e');
            sha1.update('l');
            sha1.update('l');
            sha1.update('o');
            CHECK(sha1.getHexDigest() == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
        }

        {
            SHA1 sha1;
            const char* chunks[] = {
                "Lorem ipsum dolor sit amet, ",
                "consectetur adipiscing elit, ",
                "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ",
                "Ut enim ad minim veniam, quis nostrud exercitation "
                "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
                "Duis aute irure dolor in reprehenderit in voluptate velit "
                "esse cillum dolore eu fugiat nulla pariatur. "
                "Excepteur sint occaecat cupidatat non proident, "
                "sunt in culpa qui officia deserunt mollit anim id est laborum."
            };
            for (const char* chunk : chunks)
            {
                sha1.update(chunk, std::strlen(chunk));
            }
            CHECK(sha1.getHexDigest() == "cd36b370758a259b34845084a6cc38473cb95e27");
        }

        {
            SHA1 sha1;
            sha1.update("hello");
            CHECK(sha1.getHexDigest() == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
        }

        {
            SHA1 sha1;
            std::string data = "hello";
            sha1.update(data.data(), data.size());
            CHECK(sha1.getHexDigest() == "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d");
        }
    }

    SUBCASE("digest")
    {
        SHA1 sha1;
        CHECK(
            sha1.getDigest() == SHA1::Digest{
                                    0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                    0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09,
                                }
        );
    }
}
