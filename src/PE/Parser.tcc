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
#include "easylogging++.h"

namespace LIEF {
namespace PE {

template<typename PE_T>
void Parser::build(void) {

  try {
    this->build_headers<PE_T>();
  } catch (const corrupted& e) {
    LOG(WARNING) << e.what();
  }

  LOG(DEBUG) << "[+] Decomposing Sections";

  try {
    this->build_sections();
  } catch (const corrupted& e) {
    LOG(WARNING) << e.what();
  }

  LOG(DEBUG) << "[+] Decomposing Data directories";
  try {
    this->build_data_directories<PE_T>();
  } catch (const corrupted& e) {
    LOG(WARNING) << e.what();
  }

  try {
    this->build_symbols();
  } catch (const corrupted& e) {
    LOG(WARNING) << e.what();
  }
}

template<typename PE_T>
void Parser::build_headers(void) {
  using pe_optional_header = typename PE_T::pe_optional_header;

  //DOS Header
  try {
    this->binary_->dos_header_ = {reinterpret_cast<const pe_dos_header*>(
        this->stream_->read(0, sizeof(pe_dos_header)))};

  } catch (const read_out_of_bound&) {
    throw corrupted("Dos Header corrupted");
  }


  //PE32 Header
  try {
    this->binary_->header_ = {reinterpret_cast<const pe_header*>(
      this->stream_->read(
        this->binary_->dos_header().addressof_new_exeheader(),
        sizeof(pe_header)))};
  } catch (const read_out_of_bound&) {
    throw corrupted("PE32 Header corrupted");
  }

  // Optional Header
  try {
    this->binary_->optional_header_ = {reinterpret_cast<const pe_optional_header*>(
        this->stream_->read(
          this->binary_->dos_header().addressof_new_exeheader() + sizeof(pe_header),
          sizeof(pe_optional_header)))};
  } catch (const read_out_of_bound&) {
    throw corrupted("Optional header corrupted");
  }
}

template<typename PE_T>
void Parser::build_data_directories(void) {
  using pe_optional_header = typename PE_T::pe_optional_header;

  LOG(DEBUG) << "[+] Parsing data directories";

  const uint32_t dirOffset =
      this->binary_->dos_header().addressof_new_exeheader() +
      sizeof(pe_header) +
      sizeof(pe_optional_header);
  const uint32_t nbof_datadir = DATA_DIRECTORY::NUM_DATA_DIRECTORIES;

  const pe_data_directory* dataDirectory = [&] () {
    try {
      return reinterpret_cast<const pe_data_directory*>(
        this->stream_->read(dirOffset, nbof_datadir * sizeof(pe_data_directory)));
    } catch (const read_out_of_bound&) {
      throw corrupted("Data directories corrupted");
    }
  }();

  this->binary_->data_directories_.reserve(nbof_datadir);
  for (size_t i = 0; i < nbof_datadir; ++i) {
    DataDirectory* directory = new DataDirectory{&dataDirectory[i], static_cast<DATA_DIRECTORY>(i)};

    LOG(DEBUG) << "Processing directory: " << to_string(static_cast<DATA_DIRECTORY>(i));
    LOG(DEBUG) << "- RVA: 0x" << std::hex << dataDirectory[i].RelativeVirtualAddress;
    LOG(DEBUG) << "- Size: 0x" << std::hex << dataDirectory[i].Size;
    if (directory->RVA() > 0) {
      // Data directory is not always associated with section
      const uint64_t offset = this->binary_->rva_to_offset(directory->RVA());
      try {
        directory->section_ = &(this->binary_->section_from_offset(offset));
      } catch (const LIEF::not_found&) {
          LOG(WARNING) << "Unable to find the section associated with "
                       << to_string(static_cast<DATA_DIRECTORY>(i));
      }
    }
    this->binary_->data_directories_.push_back(directory);
  }

  // Import Table
  if (this->binary_->data_directory(DATA_DIRECTORY::IMPORT_TABLE).RVA() > 0) {
    LOG(DEBUG) << "[+] Decomposing Import Table";
    const uint32_t import_rva = this->binary_->data_directory(DATA_DIRECTORY::IMPORT_TABLE).RVA();
    const uint64_t offset     = this->binary_->rva_to_offset(import_rva);

    try {
      Section& section = this->binary_->section_from_offset(offset);
      section.add_type(SECTION_TYPES::IMPORT);
    } catch (const not_found&) {
      LOG(WARNING) << "Unable to find the section associated with Import Table";
    }
    this->build_import_table<PE_T>();
  }

  // Exports
  if (this->binary_->data_directory(DATA_DIRECTORY::EXPORT_TABLE).RVA() > 0) {
    LOG(DEBUG) << "[+] Decomposing Exports";
    this->build_exports();
  }

  if (this->binary_->data_directory(DATA_DIRECTORY::CERTIFICATE_TABLE).RVA() > 0) {
    try {
      this->build_signature();
    } catch (const exception& e) {
      LOG(WARNING) << e.what();
    }
  }


  // TLS
  if (this->binary_->data_directory(DATA_DIRECTORY::TLS_TABLE).RVA() > 0) {
    LOG(DEBUG) << "[+] Decomposing TLS";

    const uint32_t import_rva = this->binary_->data_directory(DATA_DIRECTORY::TLS_TABLE).RVA();
    const uint64_t offset     = this->binary_->rva_to_offset(import_rva);
    try {
      Section& section = this->binary_->section_from_offset(offset);
      section.add_type(SECTION_TYPES::TLS);
      this->build_tls<PE_T>();
    } catch (const not_found&) {
      LOG(WARNING) << "Unable to find the section associated with TLS";
    } catch (const corrupted& e) {
      LOG(WARNING) << e.what();
    }
  }

  // Relocations
  if (this->binary_->data_directory(DATA_DIRECTORY::BASE_RELOCATION_TABLE).RVA() > 0) {

    LOG(DEBUG) << "[+] Decomposing relocations";
    const uint32_t relocation_rva = this->binary_->data_directory(DATA_DIRECTORY::BASE_RELOCATION_TABLE).RVA();
    const uint64_t offset         = this->binary_->rva_to_offset(relocation_rva);
    try {
      Section& section = this->binary_->section_from_offset(offset);
      section.add_type(SECTION_TYPES::RELOCATION);
      this->build_relocations();
    } catch (const not_found&) {
      LOG(WARNING) << "Unable to find the section associated with relocations";
    } catch (const corrupted& e) {
      LOG(WARNING) << e.what();
    }
  }


  // Debug
  if (this->binary_->data_directory(DATA_DIRECTORY::DEBUG).RVA() > 0) {

    LOG(DEBUG) << "[+] Decomposing debug";
    const uint32_t rva    = this->binary_->data_directory(DATA_DIRECTORY::DEBUG).RVA();
    const uint64_t offset = this->binary_->rva_to_offset(rva);
    try {
      Section& section = this->binary_->section_from_offset(offset);
      section.add_type(SECTION_TYPES::DEBUG);
      this->build_debug();
    } catch (const not_found&) {
      LOG(WARNING) << "Unable to find the section associated with debug";
    } catch (const corrupted& e) {
      LOG(WARNING) << e.what();
    }
  }


  // Resources
  if (this->binary_->data_directory(DATA_DIRECTORY::RESOURCE_TABLE).RVA() > 0) {

    LOG(DEBUG) << "[+] Decomposing resources";
    const uint32_t resources_rva = this->binary_->data_directory(DATA_DIRECTORY::RESOURCE_TABLE).RVA();
    const uint64_t offset        = this->binary_->rva_to_offset(resources_rva);
    try {
      Section& section  = this->binary_->section_from_offset(offset);
      section.add_type(SECTION_TYPES::RESOURCE);
      this->build_resources();
    } catch (const not_found&) {
      LOG(WARNING) << "Unable to find the section associated with resources";
    } catch (const corrupted& e) {
      LOG(WARNING) << e.what();
    }

  }
}

template<typename PE_T>
void Parser::build_import_table(void) {
  using uint__ = typename PE_T::uint;

  this->binary_->has_imports_ = true;

  const uint32_t import_rva = this->binary_->data_directory(DATA_DIRECTORY::IMPORT_TABLE).RVA();
  const uint64_t offset     = this->binary_->rva_to_offset(import_rva);

  const pe_import* header = reinterpret_cast<const pe_import*>(
    this->stream_->read(offset, sizeof(pe_import)));

  while (header->ImportAddressTableRVA != 0) {
    Import import           = {header};
    import.directory_       = &(this->binary_->data_directory(DATA_DIRECTORY::IMPORT_TABLE));
    import.iat_directory_   = &(this->binary_->data_directory(DATA_DIRECTORY::IAT));
    import.type_            = this->type_;
    if (import.name_RVA_ == 0) {
      throw parser_error("Name's RVA is null");
    }
    // Offset to the Import (Library) name
    const uint64_t offsetName = this->binary_->rva_to_offset(import.name_RVA_);
    import.name_              = this->stream_->read_string(offsetName);

    // Offset to import lookup table
    uint64_t LT_offset      = 0;
    if (import.import_lookup_table_RVA_ > 0) {
      LT_offset = this->binary_->rva_to_offset(import.import_lookup_table_RVA_);
    }

    // Offset to the import address table
    uint64_t IAT_offset        = 0;
    if (import.import_address_table_RVA_ > 0) {
      IAT_offset = this->binary_->rva_to_offset(import.import_address_table_RVA_);
    }

    const uint__ *lookupTable = nullptr, *IAT = nullptr, *table = nullptr;

    if (IAT_offset > 0) {
      try {
        IAT = reinterpret_cast<const uint__*>(
            this->stream_->read(IAT_offset, sizeof(uint__)));
        table = IAT;
      } catch (const LIEF::exception&) {
      }
    }

    if (LT_offset > 0) {
      try {
        lookupTable = reinterpret_cast<const uint__*>(
            this->stream_->read(LT_offset, sizeof(uint__)));

        table = lookupTable;
      } catch (const LIEF::exception&) {
      }

    }
    size_t idx = 0;
    while (table != nullptr and *table != 0) {
      ImportEntry entry;
      entry.iat_value_ = IAT != nullptr ? *(IAT++) : 0;
      entry.data_      = *table;
      entry.type_      = this->type_;
      entry.rva_       = import.import_address_table_RVA_ + sizeof(uint__) * (idx++);

      if(not entry.is_ordinal()) {

        entry.name_ = this->stream_->read_string(
            this->binary_->rva_to_offset(entry.hint_name_rva()) + sizeof(uint16_t));

        entry.hint_ = *reinterpret_cast<const uint16_t*>(
            this->stream_->read(
              this->binary_->rva_to_offset(entry.hint_name_rva()),
              sizeof(uint16_t)));
      }

      import.entries_.push_back(std::move(entry));

      table++;
    }
    this->binary_->imports_.push_back(std::move(import));
    header++;
  }

}

template<typename PE_T>
void Parser::build_tls(void) {
  using pe_tls = typename PE_T::pe_tls;
  using uint__ = typename PE_T::uint;

  LOG(DEBUG) << "[+] Parsing TLS";

  this->binary_->has_tls_ = true;

  const uint32_t tls_rva = this->binary_->data_directory(DATA_DIRECTORY::TLS_TABLE).RVA();
  const uint64_t offset  = this->binary_->rva_to_offset(tls_rva);

  const pe_tls *tls_header = reinterpret_cast<const pe_tls*>(
      this->stream_->read(offset, sizeof(pe_tls)));

  this->binary_->tls_ = {tls_header};
  TLS& tls = this->binary_->tls_;

  const uint64_t imagebase = this->binary_->optional_header().imagebase();

  try {
    const uint64_t startDataRVA = tls_header->RawDataStartVA - imagebase;
    const uint64_t stopDataRVA  = tls_header->RawDataEndVA - imagebase;
    const uint__ offsetStartTemplate  = this->binary_->rva_to_offset(startDataRVA);
    const uint__ offsetEndTemplate    = this->binary_->rva_to_offset(stopDataRVA);

    const uint8_t* template_ptr = reinterpret_cast<const uint8_t*>(
        this->stream_->read(offsetStartTemplate, offsetEndTemplate - offsetStartTemplate));
    std::vector<uint8_t> templateData = {
      template_ptr,
      template_ptr + offsetEndTemplate - offsetStartTemplate
    };
    tls.data_template(templateData);

  } catch (const read_out_of_bound&) {
    throw corrupted("TLS corrupted (data template)");
  } catch (const std::bad_alloc&) {
    throw corrupted("TLS corrupted (data template)");
  }

  const uint64_t offsetToCallbacks  = this->binary_->rva_to_offset(tls.addressof_callbacks() - imagebase);
  const uint__ *rvaCallbacksAddress = reinterpret_cast<const uint__*>(
      this->stream_->read(offsetToCallbacks, sizeof(uint__)));

  while (*rvaCallbacksAddress != 0) {
    tls.callbacks_.push_back(static_cast<uint64_t>(*rvaCallbacksAddress));
    rvaCallbacksAddress++;
  }

  tls.directory_ = &(this->binary_->data_directory(DATA_DIRECTORY::TLS_TABLE));

  try {
    Section& section = this->binary_->section_from_offset(offset);
    tls.section_     = &section;
  } catch (const not_found&) {
    LOG(WARNING) << "No section associated with TLS";
  }

}
}
}
