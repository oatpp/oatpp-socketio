/***************************************************************************
 *
 * Utilities.
 *
 ***************************************************************************/

#ifndef oatpp_sio_util_hpp
#define oatpp_sio_util_hpp

#include <string>

/**
 * Generate a random identifier (session ids).
 *
 * The alphabet is [0-9a-z]; @p length is the exact length of the result.
 * The values come from a non-deterministic source and do not touch the C
 * runtime RNG, so ids are unpredictable and independent of any srand() call
 * elsewhere in the process.
 *
 * @param length number of characters.
 * @return the generated string, empty for @p length <= 0.
 */
std::string generateRandomString(int length);

#endif /* oatpp_sio_util_hpp */
