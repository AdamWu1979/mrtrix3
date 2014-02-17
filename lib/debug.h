#ifndef __debug_h__
#define __debug_h__

#include "mrtrix.h"
#include "app.h"

/** \defgroup debug Debugging 
 * \brief functions and macros provided to ease debugging. */

/** \addtogroup debug
 * @{ */

/** \brief Prints the current function, file and line number. */
#define TRACE \
  std::cerr << MR::App::NAME << ": " << __PRETTY_FUNCTION__ << " (" << __FILE__ << ": " << __LINE__ << ")\n"


/** \brief Prints a variable name and its value, followed by the function, file and line number. */
#define VAR(variable) \
  std::cerr << MR::App::NAME << ": " << #variable << " = " << (variable) \
  << " (" << __PRETTY_FUNCTION__ << "; " << __FILE__  << ": " << __LINE__ << ")\n"

/** \brief Stops execution and prints current function, file and line number. 
  Remuses on user input (i.e. Return key). */
#define PAUSE { \
  std::cerr << MR::App::NAME << ": paused, " << __PRETTY_FUNCTION__ << " (" << __FILE__ << ": " << __LINE__ << ")...\n"; \
  std::string __n__; std::getline (std::cin, __n__); \
}

/** @} */

#endif

