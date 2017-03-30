/* Copyright 2017 R. Thomas
 * Copyright 2017 Quarkslab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "pyELF.hpp"

#include "LIEF/visitors/Hash.hpp"

#include "LIEF/ELF/DynamicEntryRpath.hpp"
#include "LIEF/ELF/DynamicEntry.hpp"

#include <string>
#include <sstream>

template<class T>
using getter_t = T (DynamicEntryRpath::*)(void) const;

template<class T>
using setter_t = void (DynamicEntryRpath::*)(T);

void init_ELF_DynamicEntryRpath_class(py::module& m) {

  //
  // Dynamic Entry RPATH object
  //
  py::class_<DynamicEntryRpath, DynamicEntry>(m, "DynamicEntryRpath")
    .def(py::init<const std::string &>())
    .def_property("name",
        static_cast<getter_t<const std::string&>>(&DynamicEntryRpath::name),
        static_cast<setter_t<const std::string&>>(&DynamicEntryRpath::name),
        "Return path value")

    .def_property("rpath",
        static_cast<getter_t<const std::string&>>(&DynamicEntryRpath::rpath),
        static_cast<setter_t<const std::string&>>(&DynamicEntryRpath::rpath),
        "Return path value")

    .def("__eq__", &DynamicEntryRpath::operator==)
    .def("__ne__", &DynamicEntryRpath::operator!=)
    .def("__hash__",
        [] (const DynamicEntryRpath& entry) {
          return LIEF::Hash::hash(entry);
        })


    .def("__str__",
        [] (const DynamicEntryRpath& entry)
        {
          std::ostringstream stream;
          stream << entry;
          std::string str =  stream.str();
          return str;
        });
}
