// Copyright 2016 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TYPE_SUPPORT_DISPATCH_HPP_
#define TYPE_SUPPORT_DISPATCH_HPP_

#include <dlfcn.h>
#include <unistd.h> // For readlink
#include <limits.h> // For PATH_MAX

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include "rcpputils/shared_library.hpp"
#include "rcutils/error_handling.h"
#include "rcutils/snprintf.h"
#include "rosidl_typesupport_c/identifier.h"
#include "rosidl_typesupport_c/type_support_map.h"

#include "rules_cc/cc/runfiles/runfiles.h"

using rules_cc::cc::runfiles::Runfiles;

namespace rosidl_typesupport_c
{

// Try to get the value of an environment variable, and optionally return the result.
static std::optional<std::string>
get_env_variable(const char * env_var)
{
  char * ament_prefix_path = nullptr;
#ifndef _WIN32
  ament_prefix_path = getenv(env_var);
#else
  size_t ament_prefix_path_size;
  _dupenv_s(&ament_prefix_path, &ament_prefix_path_size, env_var);
#endif
  if (!ament_prefix_path || std::string(ament_prefix_path).empty())
  {
      return {};
  }
  return std::string(ament_prefix_path);
}

extern const char * typesupport_identifier;

template<typename TypeSupport>
const TypeSupport *
get_typesupport_handle_function(
  const TypeSupport * handle, const char * identifier)
{
  if (strcmp(handle->typesupport_identifier, identifier) == 0) {
    return handle;
  }

  if (strcmp(
      handle->typesupport_identifier,
      rosidl_typesupport_c__typesupport_identifier) == 0)
  {
    const type_support_map_t * map = \
      static_cast<const type_support_map_t *>(handle->data);
    for (size_t i = 0; i < map->size; ++i) {
      if (strcmp(map->typesupport_identifier[i], identifier) != 0) {
        continue;
      }
      rcpputils::SharedLibrary * lib = nullptr;

      /* Firstly, check to see if the symbol is available in the current library
       * to cover the case where Bazel has statically linked it in. */
      void* handle = dlopen(NULL, RTLD_LAZY);
      if (handle) {
        void (*func_ptr)() = (void (*)()) dlsym(handle, map->symbol_name[i]);
        if (func_ptr) {
          typedef const TypeSupport * (* funcSignature)(void);
          funcSignature func = reinterpret_cast<funcSignature>(func_ptr);
          const TypeSupport * ts = func();
          return ts;
        }
      }
      
     /* Secondly, check to see if there is a pybind11 extension with the
         typesupport statically compiled in. */
      const std::string package_name(map->package_name);
      const std::string typesupport_name(identifier);
      const std::optional<std::string> test_workspace = get_env_variable("TEST_WORKSPACE");
      std::string error;
      auto runfiles = test_workspace
        ? Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error) 
        : Runfiles::Create("ignore-argv[0]", BAZEL_CURRENT_REPOSITORY, &error);

      // Try load statically from the pybind extension.
      if (!map->data[i]) {
        const std::string library_name = runfiles->Rlocation(
          "_main/lib" + package_name + "_s__rosidl_typesupport_c.so");
        try {
          map->data[i] = new rcpputils::SharedLibrary(library_name);
        } catch (const std::runtime_error & e) {
          RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
            "Could not load library %s: %s", library_name.c_str(), e.what());
        } catch (const std::bad_alloc & e) {
          RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
            "Could not load library %s: %s", library_name.c_str(), e.what());
        }
      }

      // Fall back to the typesupport library.
      if (!map->data[i]) {
        const std::string library_name = runfiles->Rlocation(
          "_main/lib" + package_name + "__" + typesupport_name + ".so");
        try {
          map->data[i] = new rcpputils::SharedLibrary(library_name);
        } catch (const std::runtime_error & e) {
          RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
            "Could not load library %s: %s", library_name.c_str(), e.what());
        } catch (const std::bad_alloc & e) {
          RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
            "Could not load library %s: %s", library_name.c_str(), e.what());
        }
      }

      // We can't load the library, so bail with error.
      if (!map->data[i]) {
        RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "Could not load typesupport library %s for message %s",
            typesupport_name.c_str(), package_name.c_str());
        return nullptr;
      }

      // Get the symbol from the library
      auto clib = static_cast<const rcpputils::SharedLibrary *>(map->data[i]);
      lib = const_cast<rcpputils::SharedLibrary *>(clib);

      void * sym = nullptr;

      try {
        if (!lib->has_symbol(map->symbol_name[i])) {
          RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
            "Failed to find symbol '%s' in library", map->symbol_name[i]);
          return nullptr;
        }
        sym = lib->get_symbol(map->symbol_name[i]);
      } catch (const std::exception & e) {
        RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "Failed to get symbol '%s' in library: %s",
          map->symbol_name[i], e.what());
        return nullptr;
      }

      typedef const TypeSupport * (* funcSignature)(void);
      funcSignature func = reinterpret_cast<funcSignature>(sym);
      const TypeSupport * ts = func();
      return ts;
    }
  }
  RCUTILS_SET_ERROR_MSG_WITH_FORMAT_STRING(
    "Handle's typesupport identifier (%s) is not supported by this library",
    handle->typesupport_identifier);
  return nullptr;
}

}  // namespace rosidl_typesupport_c

#endif  // TYPE_SUPPORT_DISPATCH_HPP_
