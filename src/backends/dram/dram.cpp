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

/* C includes */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

/* internal includes */
#include <logging.h>
#include <utils.h>
#include <posix-file.h>
#include <dram/file.h>
#include <dram/mapping.h>
#include <dram/snapshot.h>
#include <dram/dram.h>

namespace efsng {
namespace dram {

dram_backend::dram_backend(uint64_t capacity, logger& logger)
    : m_capacity(capacity),
      m_logger(logger) {
}

dram_backend::~dram_backend(){
}

std::string dram_backend::name() const {
    return s_name;
}

uint64_t dram_backend::capacity() const {
    return m_capacity;
}

/** start the load process of a file requested by the user */
void dram_backend::load(const bfs::path& pathname){

#ifdef __EFS_DEBUG__
    m_logger.debug("Loading file \"{}\" in RAM", pathname.string());
#endif

    /* open the file */
    posix::file fd(pathname);

    /* create a mapping and fill it with the current contents of the file */
    mapping mp(fd.get_size());
    mp.populate(fd);
    fd.close();

#ifdef __EFS_DEBUG__
    m_logger.debug("Transfer complete ({} bytes)", mp.m_bytes);
#endif

    /* add the mapping to a nvml::file descriptor and insert it into m_files */
    std::lock_guard<std::mutex> lock(m_files_mutex);

    //XXX consider using the TBB concurrent_map to discriminate between locking
    // readers and writers
    m_files.insert(std::make_pair(
        pathname.c_str(),
        std::make_unique<dram::file>(mp)
        )
    );

    /* lock_guard is automatically released here */
}

bool dram_backend::exists(const char* pathname) const {

    std::lock_guard<std::mutex> lock(m_files_mutex);

    const auto& it = m_files.find(pathname);

    // XXX it is safe to unlock the mutex here BECAUSE WE KNOW
    // that no other threads are going to remove a mapping from
    // the list; this happens because mappings are ONLY destroyed
    // when the std::list destructor is called (and we make sure that
    // this is done in a thread-safe manner). If this assumption ever
    // changes, we may NEED to revise this.
    return it != m_files.end();
}

/* build and return a list of all mapping regions affected by the read() operation */
void dram_backend::read_prepare(const file& file, off_t offset, size_t size, buffer_map& bufmap) const {


    const dram::file& f = dynamic_cast<const dram::file&>(file);

    std::list<snapshot> snaps;

    for(const auto& mp : f.m_mappings) {

        // lock the mapping to make sure that there are no changes between
        // checking if we should process it and creating an snapshot for it
        std::lock_guard<std::mutex> lock(*mp.m_pmutex);

        // if the mapping exceeds the region affected by 
        // the operation, we can stop
        if(mp.m_offset >= (off_t) (offset + size)) {
            break;
        }

        // check if the mapping is affected by the operation and, if so,
        // create a snapshot of its current state and store it for later use
        if(mp.overlaps(offset, size)) {
            snaps.emplace_back(mp);
        }

        // lock is released here
    }

#ifdef __EFS_DEBUG__
    m_logger.debug("snapshots = {");
    for(const auto& sn : snaps){
        m_logger.debug("  {}", sn);
    }
    m_logger.debug("};");
#endif

    // from this point on, threads should work EXCLUSIVELY with 
    // the snapshots to compute the data that must be returned to the user,
    // for the sake of reducing contention. We now build a buffer map based 
    // on the snapshots collected
    for(const auto& sn : snaps) {

        data_ptr_t buf_start = 0;
        size_t buf_size = 0;

        data_ptr_t mp_data = sn.m_data;
        off_t mp_offset = sn.m_offset;
        size_t mp_size = sn.m_size;
        size_t mp_bytes = sn.m_bytes;
        off_t delta = 0;

        /* compute the address where the read should start */
        if(offset <= mp_offset){
            delta = 0;
        }
        else { /* offset > mp_offset */
            delta = offset - mp_offset;
        }
        
        buf_start = (data_ptr_t)((uintptr_t) mp_data + delta);

        assert(mp_bytes <= mp_size);

        /* compute how much to read from this mapping */
        if(offset + size <= mp_offset + mp_bytes) {
            buf_size = offset + size - mp_offset - delta;
        }
        else { /* offset + size > mp_offset + mp_bytes */
            buf_size = mp_bytes - delta;
        }

#ifdef __EFS_DEBUG__
        m_logger.debug("{} + {} => {}, {}", offset, size, buf_start, buf_size);
#endif

        bufmap.emplace_back(buf_start, buf_size);
    }

    // XXX WARNING: the snapshots are currently deleted here. We don't know 
    // (yet) if this is really what we need
#ifdef __EFS_DEBUG__
    m_logger.debug("Deleting snapshots!");
#endif
}

void dram_backend::read_finalize(const file& file, off_t offset, size_t size, buffer_map& bufmap) const {
    (void) file;
    (void) offset;
    (void) size;
    (void) bufmap;
}

void dram_backend::write_prepare(file& file, off_t offset, size_t size, buffer_map& bufmap) const {
    (void) file;
    (void) offset;
    (void) size;
    (void) bufmap;
    //TODO
}

void dram_backend::write_finalize(file& file, off_t offset, size_t size, buffer_map& bufmap) const {
    (void) file;
    (void) offset;
    (void) size;
    (void) bufmap;
    //TODO
}

efsng::backend::iterator dram_backend::find(const char* path) {
    return m_files.find(path);
}

efsng::backend::iterator dram_backend::begin() {
    return m_files.begin();
}

efsng::backend::iterator dram_backend::end() {
    return m_files.end();
}

efsng::backend::const_iterator dram_backend::cbegin() {
    return m_files.cbegin();
}

efsng::backend::const_iterator dram_backend::cend() {
    return m_files.cend();
}

} // namespace dram
} //namespace efsng