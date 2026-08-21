#pragma once
#include "writer.h"

namespace dumper::writer {

    // Emits internal-only offsets (Luau VM, in-process globals) to their own
    // header, separate from the external offsets output.
    class InternalWriter : public IWriter {
      protected:
        auto get_file_extension() -> std::string override { return ".h"; }
        auto generate_header_comment(std::chrono::milliseconds elapsed_time)
            -> std::string override;
        auto generate_content() -> std::string override;
    };

    inline InternalWriter g_internal_writer;

} // namespace dumper::writer
