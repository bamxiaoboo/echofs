/*************************************************************************
 * (C) Copyright 2016 Barcelona Supercomputing Center                    *
 *                    Centro Nacional de Supercomputacion                *
 *                                                                       *
 * This file is part of the Echo Filesystem NG.                          *
 *                                                                       *
 * See AUTHORS file in the top level directory for information           *
 * regarding developers and contributors.                                *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of the GNU Lesser General Public            *
 * License as published by the Free Software Foundation; either          *
 * version 3 of the License, or (at your option) any later version.      *
 *                                                                       *
 * The Echo Filesystem NG is distributed in the hope that it will        *
 * be useful, but WITHOUT ANY WARRANTY; without even the implied         *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR               *
 * PURPOSE.  See the GNU Lesser General Public License for more          *
 * details.                                                              *
 *                                                                       *
 * You should have received a copy of the GNU Lesser General Public      *
 * License along with Echo Filesystem NG; if not, write to the Free      *
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.    *
 *                                                                       *
 *************************************************************************/

#ifndef __DATA_STORE_H__
#define __DATA_STORE_H__

#include <cstdint>
#include <string>

#include <unordered_map>

/* C includes */
#include <fuse.h>

/* internal includes */
#include <logger.h>
#include <settings.h>
#include <efs-common.h>
#include <range_lock.h>
#include <posix-file.h>
#include "errors.h"

namespace efsng {


/**
 * A pure virtual class for manipulating a backend store
 */
class backend {

public:

    //using buffer = std::pair<data_ptr_t, size_t>;

    struct buffer {
        buffer(data_ptr_t data, size_t size) // deprecated
            : m_data(data),
              m_offset(0),
              m_size(size) { }

        buffer(data_ptr_t data, off_t offset, size_t size)
            : m_data(data),
              m_offset(offset),
              m_size(size) { }

        data_ptr_t m_data;
        off_t m_offset;
        size_t m_size;
    };

    struct buffer_map : public std::list<buffer> {

        buffer_map() : m_size(0) {}

        void emplace_back(data_ptr_t data, size_t size) { // deprecated
            this->std::list<buffer>::emplace_back(data, size);
            m_size += size;
        }

        void emplace_back(data_ptr_t data, off_t offset, size_t size) {
            this->std::list<buffer>::emplace_back(data, offset, size);
            m_size += size;
        }

        size_t            m_size;
    };

    class file {

public:
        using callback_ptr = ssize_t (*)(void*, const buffer&);

        enum class type {
            temporary,
            persistent
        };

        virtual void stat(struct stat& buf) const = 0;
        virtual ssize_t get_data(off_t offset, size_t size, struct fuse_bufvec* fuse_buffer) = 0;
        virtual ssize_t put_data(off_t offset, size_t size, struct fuse_bufvec* fuse_buffer) = 0;
        virtual ssize_t append_data(off_t offset, size_t size, struct fuse_bufvec* fuse_buffer) = 0;

        virtual ~file(){}
    };

    using file_ptr = std::unique_ptr<file>;
    
    /* iterator types */
    typedef std::unordered_map<std::string, file_ptr>::iterator iterator;
    typedef std::unordered_map<std::string, file_ptr>::const_iterator const_iterator;

    enum Type {
        UNKNOWN = -1,
        DRAM = 0,
        NVRAM_NVML,

        /* add newly registered backend types ABOVE this line */
        TOTAL_COUNT
    };

protected:
    backend() {}

public:
    virtual ~backend() {}

public:
    static Type name_to_type(const std::string& name); // probably deprecated

    static backend* create_from_options(const std::string& type, const kv_list& backend_opts);

    virtual std::string name() const = 0;
    virtual uint64_t capacity() const = 0;
    virtual error_code load(const bfs::path& pathname) = 0;
    virtual error_code unload(const bfs::path& pathname) = 0;
    virtual bool exists(const char* pathname) const = 0;

    virtual iterator find(const char* path) = 0;
    virtual iterator begin() = 0;
    virtual iterator end() = 0;
    virtual const_iterator cbegin() = 0;
    virtual const_iterator cend() = 0;

private:
    static int64_t parse_size(const std::string& str);
}; // class backend

} // namespace efsng

#ifdef __EFS_DEBUG__
std::ostream& operator<<(std::ostream& os, const efsng::backend::buffer& buf);
std::ostream& operator<<(std::ostream& os, const efsng::backend::buffer_map& bmap);
#endif

#endif /* __DATA_STORE_H__ */