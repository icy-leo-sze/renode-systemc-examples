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

//=====================================================================
/// @file traffic_generator.cpp
// 
/// @brief traffic generation routines
//
//=====================================================================
//  Authors:
//    Bill Bunton, ESLX
//    Jack Donovan, ESLX
//    Charles Wilson, ESLX
//====================================================================

//====================================================================
//  Nov 06, 2008
//
//  Updated by:
//    Xiaopeng Qiu, JEDA Technologies, Inc
//    Email:  qiuxp@jedatechnologies.net
//
//  To fix violations of TLM2.0 rules, which are detected by JEDA 
//  TLM2.0 checker.
//
//====================================================================

#include "reporting.h"               	        // reporting macros
#include "traffic_generator.h"                // traffic generator declarations

#ifdef USING_EXTENSION_OPTIONAL
#include "extension_initiator_id.h"           // initiator ID extension
#endif  /* USING_EXTENSION_OPTIONAL */

using namespace std;

static const char *filename = "traffic_generator.cpp";  ///< filename for reporting

namespace {
const char* target_pattern_name(traffic_generator::target_pattern pattern)
{
  switch (pattern)
  {
    case traffic_generator::target_pattern::target201_only:
      return "target201_only";
    case traffic_generator::target_pattern::target202_only:
      return "target202_only";
    case traffic_generator::target_pattern::alternate_201_202:
      return "alternate_201_202";
    case traffic_generator::target_pattern::current_default:
      return "current_default";
  }

  return "unknown";
}

const char* read_write_mode_name(traffic_generator::read_write_mode mode)
{
  switch (mode)
  {
    case traffic_generator::read_write_mode::write_then_read:
      return "write_then_read";
    case traffic_generator::read_write_mode::read_only:
      return "read_only";
    case traffic_generator::read_write_mode::write_only:
      return "write_only";
  }

  return "unknown";
}
} // namespace

traffic_generator::workload_config::workload_config()
: transaction_count          ( 64 )
, address_stride             ( 4 )
, target_pattern_mode        ( target_pattern::current_default )
, read_write_mode_setting    ( read_write_mode::write_then_read )
, initiator_start_offset     ( sc_core::SC_ZERO_TIME )
{
}

/// Constructor

SC_HAS_PROCESS(traffic_generator);

//-----------------------------------------------------------------------------
traffic_generator::traffic_generator            // @todo keep me, lose other constructor
( sc_core::sc_module_name name                  // instance name
, const unsigned int    ID                      // initiator ID
, sc_dt::uint64         base_address_1          // first base address
, sc_dt::uint64         base_address_2          // second base address
, unsigned int          max_txns                // Max number of active transactions
)
: traffic_generator
  ( name
  , ID
  , base_address_1
  , base_address_2
  , max_txns
  , workload_config()
  )
{
}

traffic_generator::traffic_generator
( sc_core::sc_module_name name                  // instance name
, const unsigned int    ID                      // initiator ID
, sc_dt::uint64         base_address_1          // first base address
, sc_dt::uint64         base_address_2          // second base address
, unsigned int          max_txns                // Max number of active transactions
, const workload_config& workload              // workload knobs
)

: sc_module           ( name              )     /// instance name
, m_ID                ( ID                )     /// initiator ID
, m_base_address_1    ( base_address_1    )     /// first base address
, m_base_address_2    ( base_address_2    )     /// second base address
, m_workload_config   ( workload          )     /// workload knobs
, m_active_txn_count  ( 0                 )     /// number of active transactions
, m_check_all         ( true              )
{
  SC_THREAD(traffic_generator_thread);

  // build transaction pool
  for (unsigned int i = 0; i < max_txns; i++ )
  {
    m_transaction_queue.enqueue ();
  }
}

/// SystemC thread for generation of GP traffic

void
traffic_generator::traffic_generator_thread
( void
)
{
  std::ostringstream  msg;                      ///< log message

  msg.str ("");
  msg << "Initiator: " << m_ID << " Starting Traffic"
      << " transaction_count=" << m_workload_config.transaction_count
      << " address_stride=" << m_workload_config.address_stride
      << " target_pattern=" << target_pattern_name(m_workload_config.target_pattern_mode)
      << " read_write_mode=" << read_write_mode_name(m_workload_config.read_write_mode_setting)
      << " initiator_start_offset_ns="
      << (m_workload_config.initiator_start_offset.to_seconds() * 1e9);
  REPORT_INFO(filename, __FUNCTION__, msg.str());

  if (m_workload_config.initiator_start_offset > sc_core::SC_ZERO_TIME)
  {
    wait(m_workload_config.initiator_start_offset);
  }

  if (m_workload_config.target_pattern_mode == target_pattern::current_default)
  {
    run_current_default_workload();
  }
  else
  {
    run_pattern_workload();
  }

  msg.str ("");
  msg << "Traffic Generator : " << m_ID << endl
  << "=========================================================" << endl
  << "            ####  Traffic Generator Complete  #### ";
  REPORT_INFO(filename, __FUNCTION__, msg.str());
} // end traffic_generator_thread

void traffic_generator::run_current_default_workload(void)
{
  const unsigned int writes = write_count();
  const unsigned int reads  = read_count();

  const unsigned int target1_writes = target_phase_count(writes, true);
  const unsigned int target2_writes = target_phase_count(writes, false);
  const unsigned int target1_reads  = target_phase_count(reads, true);
  const unsigned int target2_reads  = target_phase_count(reads, false);

  run_current_default_phase(tlm::TLM_WRITE_COMMAND, m_base_address_1, target1_writes);
  run_current_default_phase(tlm::TLM_READ_COMMAND,  m_base_address_1, target1_reads);
  run_current_default_phase(tlm::TLM_WRITE_COMMAND, m_base_address_2, target2_writes);
  run_current_default_phase(tlm::TLM_READ_COMMAND,  m_base_address_2, target2_reads);
}

void traffic_generator::run_pattern_workload(void)
{
  const unsigned int writes = write_count();
  const unsigned int reads  = read_count();

  for (unsigned int i = 0; i < writes; i++ )
  {
    issue_transaction(tlm::TLM_WRITE_COMMAND, pattern_address(i, writes));
  }
  check_all_complete();

  const unsigned int read_address_count = writes ? writes : reads;
  for (unsigned int i = 0; i < reads; i++ )
  {
    issue_transaction(tlm::TLM_READ_COMMAND, pattern_address(i, read_address_count));
  }
  check_all_complete();
}

void traffic_generator::run_current_default_phase
( tlm::tlm_command command
, sc_dt::uint64 base_address
, unsigned int count
)
{
  for (unsigned int i = 0; i < count; i++ )
  {
    issue_transaction(command, base_address + (i * m_workload_config.address_stride));
  }
  check_all_complete();
}

void traffic_generator::issue_transaction
( tlm::tlm_command command
, sc_dt::uint64 mem_address
)
{
  while (m_transaction_queue.is_empty())
  {
    check_complete();
  }

  tlm::tlm_generic_payload  *transaction_ptr = m_transaction_queue.dequeue();
  transaction_ptr->acquire();
  ++m_active_txn_count;

  unsigned char *data_buffer_ptr = transaction_ptr->get_data_ptr();
  if (command == tlm::TLM_WRITE_COMMAND)
  {
    unsigned int w_data = (unsigned int)mem_address;
    if (is_second_target_address(mem_address))
    {
      w_data = ~w_data;
    }
    *reinterpret_cast<unsigned int*>(data_buffer_ptr) = w_data;
  }

  transaction_ptr->set_command          ( command                       );
  transaction_ptr->set_address          ( mem_address                   );
  transaction_ptr->set_data_length      ( m_txn_data_size               );
  transaction_ptr->set_streaming_width  ( m_txn_data_size               );
  transaction_ptr->set_response_status  ( tlm::TLM_INCOMPLETE_RESPONSE  );

  #if (  defined ( USING_EXTENSION_OPTIONAL  ) )

  extension_initiator_id  *extension_pointer;   // extension pointer
  std::ostringstream       initiator_id;        // initiator ID string

  initiator_id << "Initiator ID: " << m_ID;

  extension_pointer                 = new extension_initiator_id;
  extension_pointer->m_initiator_id = initiator_id.str();

  transaction_ptr->set_extension ( extension_pointer );

  #endif  /* USING_EXTENSION_OPTIONAL */

  request_out_port->write (transaction_ptr);
  check_complete();
}

sc_dt::uint64 traffic_generator::pattern_address
( unsigned int index
, unsigned int address_count
) const
{
  switch (m_workload_config.target_pattern_mode)
  {
    case target_pattern::target201_only:
      return m_base_address_1 + (index * m_workload_config.address_stride);
    case target_pattern::target202_only:
      return m_base_address_2 + (index * m_workload_config.address_stride);
    case target_pattern::alternate_201_202:
      return (index % 2)
          ? m_base_address_2 + ((index / 2) * m_workload_config.address_stride)
          : m_base_address_1 + ((index / 2) * m_workload_config.address_stride);
    case target_pattern::current_default:
    default:
    {
      const unsigned int target1_count = target_phase_count(address_count, true);
      if (index < target1_count)
      {
        return m_base_address_1 + (index * m_workload_config.address_stride);
      }
      return m_base_address_2 + ((index - target1_count) * m_workload_config.address_stride);
    }
  }
}

unsigned int traffic_generator::write_count(void) const
{
  switch (m_workload_config.read_write_mode_setting)
  {
    case read_write_mode::read_only:
      return 0;
    case read_write_mode::write_only:
      return m_workload_config.transaction_count;
    case read_write_mode::write_then_read:
    default:
      return (m_workload_config.transaction_count + 1) / 2;
  }
}

unsigned int traffic_generator::read_count(void) const
{
  switch (m_workload_config.read_write_mode_setting)
  {
    case read_write_mode::read_only:
      return m_workload_config.transaction_count;
    case read_write_mode::write_only:
      return 0;
    case read_write_mode::write_then_read:
    default:
      return m_workload_config.transaction_count / 2;
  }
}

unsigned int traffic_generator::target_phase_count
( unsigned int count
, bool first_target
) const
{
  return first_target ? ((count + 1) / 2) : (count / 2);
}

bool traffic_generator::verify_read_data(void) const
{
  return m_workload_config.read_write_mode_setting != read_write_mode::read_only;
}

bool traffic_generator::is_second_target_address(sc_dt::uint64 mem_address) const
{
  return (mem_address & 0xF0000000ULL) == (m_base_address_2 & 0xF0000000ULL);
}

//-----------------------------------------------------------------------------
//  Check Complete method

void traffic_generator::check_complete (void)
{
  std::ostringstream        msg;   
  tlm::tlm_generic_payload  *transaction_ptr;
    
  if (   m_transaction_queue.is_empty() 
      || m_check_all 
      || ( response_in_port->num_available() > 0 ) )
  {
    response_in_port->read(transaction_ptr);
    
    if (transaction_ptr ->get_response_status() != tlm::TLM_OK_RESPONSE)
    {
      msg.str ("");
      msg << m_ID << "Transaction ERROR";
      REPORT_FATAL(filename, __FUNCTION__, msg.str()); 
    }
    
    if (  transaction_ptr->get_command() == tlm::TLM_READ_COMMAND
       && verify_read_data())
    {
      unsigned int    expected_data   = (unsigned int)transaction_ptr->get_address();
      unsigned char*  data_buffer_ptr = transaction_ptr->get_data_ptr();
      unsigned int    read_data       = *reinterpret_cast<unsigned int*>(data_buffer_ptr);
    
      //-----------------------------------------------------------------------------
      // The address for the “gp” is used as expected data.  The address filed of 
      //  the “gp” is a mutable field and is changed by the SimpleBus interconnect. 
      //  The list significant 28 bits are not modified and are use for comparison.    

      const unsigned int data_mask ( 0x0FFFFFFF );
      
      unsigned int read_data_masked = read_data & data_mask;
    
      if (   ( read_data_masked != (  expected_data & data_mask ) )
          && ( read_data_masked != ( ~expected_data & data_mask ) ) )
      {
        msg.str ("");
        msg << m_ID << " Memory read data ERROR";
        REPORT_FATAL(filename, __FUNCTION__, msg.str()); 
      }
    }
    transaction_ptr->release();
    --m_active_txn_count;
  }
} // end check_complete

//-----------------------------------------------------------------------------
//  Check All Complete method

void
traffic_generator::check_all_complete
( void
)
{
  while (m_active_txn_count)
  {
    m_check_all = true; 
    check_complete();
  }
  
  m_check_all = false; 
} // end check_all_complete
