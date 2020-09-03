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
///  @file example_main.cpp
///
///  @brief Example main instantiates the example system top and starts 
///  the systemc simulator
///
//=====================================================================
//  Original Authors:
//    Bill Bunton, ESLX
//    Anna Keist, ESLX
//    Charles Wilson, ESLX
//    Jack Donovan, ESLX
//=====================================================================

// define REPORT_DEFINE_GLOBALS in one location only

#define REPORT_DEFINE_GLOBALS

#include "reporting.h"                  // reporting utilities
#include "at_1_phase_top.h"             // top module
#include "tlm.h"                        // TLM header
#include "sparta/sparta.hpp"
#include <iostream>
#include <cinttypes>

#include "systemc.h"
#include "sparta/ports/DataPort.hpp"
#include "sparta/ports/PortSet.hpp"
#include "sparta/kernel/Scheduler.hpp"
#include "sparta/utils/SpartaTester.hpp"
#include "sparta/utils/SysCSpartaSchedulerAdapter.hpp"


TEST_INIT;

/*
 * Hammers on the scheduler
 */
uint64_t expected_cycle = 1;

typedef sparta::DataInPort<uint32_t> DataInPortType;
typedef sparta::DataOutPort<uint32_t> DataOutPortType;
bool first_called = false;
uint32_t events_fired = 0;

class InAndDataOutPort : sparta::TreeNode
{
    sparta::PortSet ps_;
public:
    InAndDataOutPort(sparta::TreeNode *parent, const std::string& name, sparta::Clock* clk) :
        TreeNode(parent, name, "description"),
        ps_(this, "inanddataoutport_ps"),
        name_(name),
        in_port_(&ps_, "in_"+name),
        out_port_(&ps_, "out_"+name)
    {
        //Bind a callback to the inport.
        in_port_.registerConsumerHandler(CREATE_SPARTA_HANDLER_WITH_DATA(InAndDataOutPort, callback, uint32_t));
    }

    void callback(const uint32_t &)
    {
        ++events_fired;
    }

    //!Set this object's in port as dependent upon another port.
    void addDependency(InAndDataOutPort& helper)
    {
        helper.getDataInPort().precedes(in_port_);
        sparta::bind(out_port_, in_port_);
    }
    void bind()
    {
        sparta::bind(out_port_, in_port_);
    }
    DataInPortType& getDataInPort() { return in_port_;}
    DataOutPortType& getDataOutPort() {return out_port_; }

    void fire()
    {
        out_port_.send(5, 50);
    }

    uint32_t getPrecedenceGroup()
    {
        return 0;
        //return in_port_.getScheduleable().getGroupID();
    }
private:
    std::string name_;
    DataInPortType in_port_;
    DataOutPortType out_port_;

};

//!Set up and test that port's are fired in order of their dependencies.
class DependencyTest
{
public:
    // Dependency tree being built (in-port group IDs for each node)
    //
    // X (3) --.------------------> B (2) -.--> A (1)
    // Y (3) --|                           |
    // C (3) --'  F (4) -> Z (3) -> W (2) -'
    //
    //
    DependencyTest(sparta::TreeNode * parent, sparta::Clock* clk) :
        a(parent, "A", clk),
        b(parent, "B", clk),
        w(parent, "W", clk),
        z(parent, "Z", clk),
        x(parent, "X", clk),
        y(parent, "Y", clk),
        c(parent, "C", clk),
        f(parent, "F", clk),
        clk_(clk)
    {

        //Build up some precedence
        a.bind();
        b.addDependency(a);
        w.addDependency(a);
        z.addDependency(w);
        f.addDependency(z);
        x.addDependency(b);
        y.addDependency(b);
        c.addDependency(b);
    }

    void checkDagFinalization()
    {
        EXPECT_EQUAL(clk_->getScheduler()->getDAG()->numGroups(), 17);
    }
    void fire()
    {
        //Call a fire on a bunch of ports all at the same cycle,
        //see if our output is what was expected.
        c.fire();
        a.fire();
        x.fire();
        f.fire();
        y.fire();
        z.fire();
        b.fire();
        w.fire();
    }

    //Create some helper objects.
    InAndDataOutPort a;
    InAndDataOutPort b;
    InAndDataOutPort w;
    InAndDataOutPort z;
    InAndDataOutPort x;
    InAndDataOutPort y;
    InAndDataOutPort c;
    InAndDataOutPort f;
    sparta::Clock* clk_ = nullptr;
};
//=====================================================================
///  @fn sc_main
//  
///  @brief SystemC entry point
//  
///  @details
///    This is the SystemC entry point for the example system.  The argc and argv 
///    parameters are not used.  Simulation runtime is not specified when 
///    sc_start() is called, the examples traffic generator will run to 
///    completion, ending the simulation. 
///
//=====================================================================
int                                     // return status
sc_main                                 // SystemC entry point
  (int   argc                           // argument count
  ,char  *argv[]                        // argument vector
)
{
  REPORT_ENABLE_ALL_REPORTING ();
  
  sparta::Scheduler sched;
  sparta::Clock clk("clock", &sched);

  EXPECT_TRUE(sched.getCurrentTick() == 1);
  EXPECT_TRUE(sched.isRunning() == 0);
  // Enable scheduler logging. Find the scheduler node and setup basic DEBUG message
  std::vector<sparta::TreeNode*> roots;
  sparta::TreeNode::getVirtualGlobalNode()->findChildren(sparta::Scheduler::NODE_NAME, roots);
  EXPECT_EQUAL(roots.size(), 1);
  sparta::log::Tap scheduler_debug(sparta::TreeNode::getVirtualGlobalNode(),
                                     sparta::log::categories::DEBUG, "scheduler.debug");

  // Set up a dummy simulation
  sparta::RootTreeNode rtn("dummyrtn");
  rtn.setClock(&clk);
  //Test port dependency
  DependencyTest   test(&rtn, &clk);
  sparta::EventSet event_set(&rtn);
  sparta::Event    fire_event(&event_set, "fire_event",
                                CREATE_SPARTA_HANDLER_WITH_OBJ(DependencyTest, &test, fire));
  sched.finalize();
  test.checkDagFinalization();
  fire_event.schedule(1);
  sched.printNextCycleEventTree(std::cout, 0, 0);

  sparta::SysCSpartaSchedulerAdapter sysc_sched_runner(&sched);
  example_system_top top("top");        // instantiate a exmaple top module

    // Run simulation
  sysc_sched_runner.run();

    // This is where Sparta left off...
  EXPECT_EQUAL(sched.getCurrentTick(), 53);

    // SysC saturation -- end of time
  EXPECT_EQUAL(sc_core::sc_time_stamp().value(), 0x8000000000000000);

  EXPECT_EQUAL(events_fired, 8);

    // Compare the schedler log output with the expected to ensure it is logging
  EXPECT_FILES_EQUAL("scheduler.debug.EXPECTED", "scheduler.debug");

  rtn.enterTeardown();

    // Returns error
  REPORT_ERROR;
  return 0;                             // return okay status
}
