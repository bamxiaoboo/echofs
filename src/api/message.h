/*************************************************************************
 * (C) Copyright 2016-2017 Barcelona Supercomputing Center               *
 *                         Centro Nacional de Supercomputacion           *
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

 /*
* This software was developed as part of the
* EC H2020 funded project NEXTGenIO (Project ID: 671951)
* www.nextgenio.eu
*/ 


#ifndef __API_MESSAGE_H__
#define __API_MESSAGE_H__

//#include "requests.hpp"
//#include "responses.hpp"
#include <arpa/inet.h>
#include <iomanip>
#include <iostream> 

namespace {

uint64_t htonll(uint64_t x) {
    return ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32));
}

uint64_t ntohll(uint64_t x) {
    return ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32));
}

}

namespace efsng {
namespace api {

// class message implements simple "packing/unpacking" of protocol buffers messages
// produced by libprotobuf-c
template <typename Request, typename Response>
class message {

    using request_ptr = std::shared_ptr<Request>;
    using response_ptr = std::shared_ptr<Response>;

public:

    typedef Request request_type;
    typedef Response response_type;

    enum part { header, body };

    static const std::size_t HEADER_LENGTH = sizeof(uint64_t);

    message() :
        m_header(HEADER_LENGTH),
        m_header_length(0),
        m_body_length(0),
        m_expected_body_length(0) { }

    static void cleanup() {
        Request::cleanup();
        Response::cleanup();
    }

    std::size_t expected_length(const message::part part) const {

        switch(part) {
            case header:
                return HEADER_LENGTH;
            case body:
                return m_expected_body_length;
        }

        return 0;
    }

    std::size_t length(const message::part part) const {

        switch(part) {
            case header:
                return m_header_length;
            case body:
                return m_body_length;
        }

        return 0;
    }

    std::vector<uint8_t>& buffer(const message::part part) {
        switch(part) {
            case header:
                return m_header;
            case body:
                return m_body;
        }

        // should never reach here
        assert(false);
        return m_header; // to avoid compiler warning
    }

    void clear() {
        m_header.clear();
        m_body.clear();
        m_header_length = m_body_length = m_expected_body_length = 0;
    }

    bool encode_response(response_ptr resp) {

        if(!Response::store_to_buffer(resp, m_body)) {
            return false;
        }

        m_body_length = m_body.size();

        m_header.resize(HEADER_LENGTH);

        uint64_t* pbuf = (uint64_t*) m_header.data();
        *pbuf = ::htonll(m_body_length);

        m_header_length = HEADER_LENGTH;

        return true;
    }

    bool decode_header(std::size_t msg_length) {

        if(msg_length != HEADER_LENGTH) {
            return false;
        }

        m_header_length = HEADER_LENGTH;
        m_expected_body_length = ::ntohll(*((uint64_t*) m_header.data()));

        m_body.resize(m_expected_body_length);

        return true;
    }

    request_ptr decode_body(std::size_t msg_length) {
        return Request::create_from_buffer(m_body, msg_length);
    }

    static void print_hex(const std::vector<uint8_t>& buffer) {
        std::cerr << "<< ";
        for(size_t i=0; i<buffer.size(); ++i) {
            std::cerr << std::setfill('0') << std::setw(2) << std::hex << (int)buffer[i] << " " << std::dec;
        }
        std::cerr << " >>\n";
    }


private:
    std::vector<uint8_t>    m_header;
    std::size_t             m_header_length;
    std::vector<uint8_t>    m_body;
    std::size_t             m_body_length;
    std::size_t             m_expected_body_length;
};

} // namespace api
} // namespace efsng

#endif /* __API_MESSAGE_H__ */
