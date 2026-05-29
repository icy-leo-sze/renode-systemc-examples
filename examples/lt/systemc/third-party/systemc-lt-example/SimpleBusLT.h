/*****************************************************************************

  Licensed to Accellera Systems Initiative Inc. (Accellera) under one or
  more contributor license agreements.  See the NOTICE file distributed
  with this work for additional information regarding copyright ownership.
  Accellera licenses this file to you under the Apache License, Version 2.0
  (the "License"); you may not use this file except in compliance with the
  License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
  implied.  See the License for the specific language governing
  permissions and limitations under the License.

 *****************************************************************************/

#ifndef __SIMPLEBUSLT_H__
#define __SIMPLEBUSLT_H__

//#include <systemc>
#include "tlm.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>

#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/simple_initiator_socket.h"

template <int NR_OF_INITIATORS, int NR_OF_TARGETS>
class SimpleBusLT : public sc_core::sc_module
{
public:
  typedef tlm::tlm_generic_payload                 transaction_type;
  typedef tlm::tlm_phase                           phase_type;
  typedef tlm::tlm_sync_enum                       sync_enum_type;
  typedef tlm_utils::simple_target_socket_tagged<SimpleBusLT>    target_socket_type;
  typedef tlm_utils::simple_initiator_socket_tagged<SimpleBusLT> initiator_socket_type;

public:
  target_socket_type target_socket[NR_OF_INITIATORS];
  initiator_socket_type initiator_socket[NR_OF_TARGETS];

public:
  SC_HAS_PROCESS(SimpleBusLT);
  SimpleBusLT(sc_core::sc_module_name name) :
    sc_core::sc_module(name)
  {
    for (unsigned int i = 0; i < NR_OF_INITIATORS; ++i) {
      target_socket[i].register_b_transport(this, &SimpleBusLT::initiatorBTransport, i);
      target_socket[i].register_transport_dbg(this, &SimpleBusLT::transportDebug, i);
      target_socket[i].register_get_direct_mem_ptr(this, &SimpleBusLT::getDMIPointer, i);
    }
    for (unsigned int i = 0; i < NR_OF_TARGETS; ++i) {
      initiator_socket[i].register_invalidate_direct_mem_ptr(this, &SimpleBusLT::invalidateDMIPointers, i);
      m_target_busy_until[i] = sc_core::SC_ZERO_TIME;
    }
  }

  //
  // Dummy decoder:
  // - address[31-28]: portId
  // - address[27-0]: masked address
  //

  unsigned int getPortId(const sc_dt::uint64& address)
  {
    return (unsigned int)address >> 28;
  }

  sc_dt::uint64 getAddressOffset(unsigned int portId)
  {
    return portId << 28;
  }

  sc_dt::uint64 getAddressMask(unsigned int /*portId*/)
  {
    return 0xfffffff;
  }

  unsigned int decode(const sc_dt::uint64& address)
  {
    // decode address:
    // - return initiator socket id

    return getPortId(address);
  }

  //
  // interface methods
  //

  //
  // LT protocol
  // - forward each call to the target/initiator
  //
  void initiatorBTransport(int SocketId,
                           transaction_type& trans,
                           sc_core::sc_time& t)
  {
    initiator_socket_type* decodeSocket;
    sc_dt::uint64 originalAddress = trans.get_address();
    sc_core::sc_time requestTime = sc_core::sc_time_stamp();
    sc_core::sc_time beforeDelay = t;
    sc_core::sc_time currentTime = requestTime + beforeDelay;
    double startTimeNs = currentTime.to_seconds() * 1e9;
    unsigned int portId = decode(originalAddress);
    assert(portId < NR_OF_TARGETS);
    decodeSocket = &initiator_socket[portId];
    trans.set_address(trans.get_address() & getAddressMask(portId));
    sc_dt::uint64 maskedAddress = trans.get_address();

    sc_core::sc_time grantTime =
        (m_target_busy_until[portId] > currentTime) ? m_target_busy_until[portId]
                                                    : currentTime;
    sc_core::sc_time queueDelay = grantTime - currentTime;
    t += queueDelay;
    sc_core::sc_time beforeTargetServiceDelay = t;

    (*decodeSocket)->b_transport(trans, t);

    sc_core::sc_time afterDelay = t;
    sc_core::sc_time targetServiceDelay = afterDelay - beforeTargetServiceDelay;
    sc_core::sc_time transactionDelay = afterDelay - beforeDelay;
    m_target_busy_until[portId] = grantTime + targetServiceDelay;

    writeLatencyTrace(SocketId, portId, trans, originalAddress, maskedAddress,
                      startTimeNs, transactionDelay, requestTime, grantTime,
                      queueDelay, targetServiceDelay,
                      m_target_busy_until[portId]);
  }

  unsigned int transportDebug(int /*SocketId*/,
                              transaction_type& trans)
  {
    unsigned int portId = decode(trans.get_address());
    assert(portId < NR_OF_TARGETS);
    initiator_socket_type* decodeSocket = &initiator_socket[portId];
    trans.set_address( trans.get_address() & getAddressMask(portId) );
    
    return (*decodeSocket)->transport_dbg(trans);
  }

  bool limitRange(unsigned int portId, sc_dt::uint64& low, sc_dt::uint64& high)
  {
    sc_dt::uint64 addressOffset = getAddressOffset(portId);
    sc_dt::uint64 addressMask = getAddressMask(portId);

    if (low > addressMask) {
      // Range does not overlap with addressrange for this target
      return false;
    }

    low += addressOffset;
    if (high > addressMask) {
      high = addressOffset + addressMask;

    } else {
      high += addressOffset;
    }
    return true;
  }

  bool getDMIPointer(int /*SocketId*/,
                     transaction_type& trans,
                     tlm::tlm_dmi&  dmi_data)
  {
    sc_dt::uint64 address = trans.get_address();

    unsigned int portId = decode(address);
    assert(portId < NR_OF_TARGETS);
    initiator_socket_type* decodeSocket = &initiator_socket[portId];
    sc_dt::uint64 maskedAddress = address & getAddressMask(portId);

    trans.set_address(maskedAddress);

    bool result =
      (*decodeSocket)->get_direct_mem_ptr(trans, dmi_data);
    
    if (result)
    {
      // Range must contain address
      assert(dmi_data.get_start_address() <= maskedAddress);
      assert(dmi_data.get_end_address() >= maskedAddress);
    }
    
    // Should always succeed
	sc_dt::uint64 start, end;
	start = dmi_data.get_start_address();
	end = dmi_data.get_end_address();
	
	limitRange(portId, start, end);
	
	dmi_data.set_start_address(start);
	dmi_data.set_end_address(end);

    return result;
  }

  void invalidateDMIPointers(int port_id,
                             sc_dt::uint64 start_range,
                             sc_dt::uint64 end_range)
  {
    // FIXME: probably faster to always invalidate everything?

    if (!limitRange(port_id, start_range, end_range)) {
      // Range does not fall into address range of target
      return;
    }
    
    for (unsigned int i = 0; i < NR_OF_INITIATORS; ++i) {
      (target_socket[i])->invalidate_direct_mem_ptr(start_range, end_range);
    }
  }

private:
  sc_core::sc_time m_target_busy_until[NR_OF_TARGETS];

  static int mapInitiatorId(int socketId)
  {
    switch (socketId) {
      case 0:
        return 101;
      case 1:
        return 102;
      case 2:
        return 9002;
      default:
        return -1;
    }
  }

  static int mapTargetId(unsigned int portId)
  {
    switch (portId) {
      case 0:
        return 201;
      case 1:
        return 202;
      default:
        return -1;
    }
  }

  static const char* commandToString(tlm::tlm_command command)
  {
    switch (command) {
      case tlm::TLM_READ_COMMAND:
        return "READ";
      case tlm::TLM_WRITE_COMMAND:
        return "WRITE";
      default:
        return "OTHER";
    }
  }

  static std::uint32_t payloadDataWord(transaction_type& trans)
  {
    std::uint32_t data = 0;
    if (trans.get_data_ptr() && trans.get_data_length() >= sizeof(data)) {
      std::memcpy(&data, trans.get_data_ptr(), sizeof(data));
    }
    return data;
  }

  static std::mutex& latencyTraceMutex()
  {
    static std::mutex mutex;
    return mutex;
  }

  static std::filesystem::path latencyTracePath()
  {
    static const std::filesystem::path path = [] {
      std::error_code error;
      std::filesystem::path executablePath =
          std::filesystem::read_symlink("/proc/self/exe", error);
      if (error) {
        std::cerr << "[latency_csv] failed to resolve /proc/self/exe: "
                  << error.message() << std::endl;
        return std::filesystem::path();
      }

      return executablePath.parent_path().parent_path() / "results" /
             "latency_trace.csv";
    }();

    return path;
  }

  static const char* latencyTraceHeader()
  {
    return "initiator_id,target_id,command,address,data,start_time_ns,"
           "delay_ns,end_time_ns,decoded_port,masked_address,data_length,"
           "response_status,request_time_ns,bus_grant_time_ns,queue_delay_ns,"
           "target_service_delay_ns,total_delay_ns,target_busy_until_ns";
  }

  static bool checkLatencyTraceHeader(const std::filesystem::path& path,
                                      bool& writeHeader)
  {
    writeHeader = false;
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
      writeHeader = true;
      return true;
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0) {
      writeHeader = true;
      return true;
    }

    std::ifstream trace(path);
    std::string header;
    std::getline(trace, header);
    if (header != latencyTraceHeader()) {
      std::cerr << "[latency_csv] existing CSV header does not match current "
                   "schema; remove "
                << path << " before rerunning" << std::endl;
      return false;
    }

    return true;
  }

  static bool ensureLatencyTraceDirectory(const std::filesystem::path& tracePath)
  {
    std::error_code error;
    std::filesystem::create_directories(tracePath.parent_path(), error);
    if (error) {
      std::cerr << "[latency_csv] failed to create directory "
                << tracePath.parent_path() << ": " << error.message()
                << std::endl;
      return false;
    }

    return true;
  }

  static void writeLatencyTrace(int socketId,
                                unsigned int portId,
                                transaction_type& trans,
                                sc_dt::uint64 originalAddress,
                                sc_dt::uint64 maskedAddress,
                                double startTimeNs,
                                sc_core::sc_time transactionDelay,
                                sc_core::sc_time requestTime,
                                sc_core::sc_time grantTime,
                                sc_core::sc_time queueDelay,
                                sc_core::sc_time targetServiceDelay,
                                sc_core::sc_time targetBusyUntil)
  {
    std::lock_guard<std::mutex> lock(latencyTraceMutex());
    const std::filesystem::path tracePath = latencyTracePath();
    if (tracePath.empty() || !ensureLatencyTraceDirectory(tracePath)) {
      return;
    }

    bool writeHeader = false;
    if (!checkLatencyTraceHeader(tracePath, writeHeader)) {
      return;
    }

    std::ofstream trace(tracePath, std::ios::out | std::ios::app);
    if (!trace) {
      std::cerr << "[latency_csv] failed to open " << tracePath << std::endl;
      return;
    }

    static bool pathAnnounced = false;
    if (!pathAnnounced) {
      std::cerr << "[latency_csv] writing to " << tracePath << std::endl;
      pathAnnounced = true;
    }

    if (writeHeader) {
      trace << latencyTraceHeader() << '\n';
    }

    double delayNs = transactionDelay.to_seconds() * 1e9;
    double endTimeNs = startTimeNs + delayNs;
    double requestTimeNs = requestTime.to_seconds() * 1e9;
    double grantTimeNs = grantTime.to_seconds() * 1e9;
    double queueDelayNs = queueDelay.to_seconds() * 1e9;
    double targetServiceDelayNs = targetServiceDelay.to_seconds() * 1e9;
    double targetBusyUntilNs = targetBusyUntil.to_seconds() * 1e9;

    trace << mapInitiatorId(socketId) << ','
          << mapTargetId(portId) << ','
          << commandToString(trans.get_command()) << ','
          << "0x" << std::uppercase << std::hex << std::setw(16)
          << std::setfill('0') << static_cast<unsigned long long>(originalAddress)
          << ','
          << "0x" << std::uppercase << std::hex << std::setw(8)
          << std::setfill('0') << payloadDataWord(trans) << ','
          << std::dec << std::setfill(' ') << std::fixed << std::setprecision(3)
          << startTimeNs << ','
          << delayNs << ','
          << endTimeNs << ','
          << portId << ','
          << "0x" << std::uppercase << std::hex << std::setw(16)
          << std::setfill('0') << static_cast<unsigned long long>(maskedAddress)
          << ','
          << std::dec << std::setfill(' ') << trans.get_data_length() << ','
          << trans.get_response_string() << ','
          << std::fixed << std::setprecision(3)
          << requestTimeNs << ','
          << grantTimeNs << ','
          << queueDelayNs << ','
          << targetServiceDelayNs << ','
          << delayNs << ','
          << targetBusyUntilNs << '\n';
  }

};

#endif
