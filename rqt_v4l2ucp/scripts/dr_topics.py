#!/usr/bin/env python
# Lucas Walter
# April 2016
# Create a dynamic reconfigure server dynamically from rosparams
# which will hook them up to topics.
# For v4l2ucp it would be nice to be able to do this in C++
# or alternatively do the v4l2 calls in python.

import copy
import rospy

from dynamic_reconfigure.server import Server
from rqt_v4l2ucp import base_cfg
from std_msgs.msg import Int32


class DrTopics():
    def __init__(self):
        self.pubs = {}
        self.subs = {}

        print dir(base_cfg)
        base_cfg.all_level = 1
        #rospy.loginfo(rospy.get_namespace())
        # TODO(lucasw) maybe this should be a pickled string instead
        # of a bunch of params?
        all_params = rospy.get_param_names()
        prefix = rospy.get_namespace() + "controls/"
        prefix_feedback = rospy.get_namespace() + "feedback/"
        for param in all_params:
            if param.find(prefix) >= 0:
                if param.find("_min") < 0 and param.find("_max") < 0 and \
                        param.find("_type") < 0:

                    param = param.replace(prefix, "")
                    base_cfg.min[param] = rospy.get_param(prefix + param + "_min")
                    base_cfg.max[param] = rospy.get_param(prefix + param + "_max")
                    # TODO(lucasw) set the default somewhere
                    base_cfg.defaults[param] = base_cfg.min[param]
                    base_cfg.type[param] = rospy.get_param(prefix + param + "_type")
                    if base_cfg.type[param] == 'menu' or \
                            base_cfg.type[param] == 'menu' or \
                            base_cfg.type[param] == 'button':
                        base_cfg.type[param] == 'int'
                    base_cfg.level[param] = 1
                    # rospy.loginfo(param + " " + str(minimum) + " " +
                    #               str(maximum) + " " + str(ctrl_type))
                    parameter = copy.deepcopy(base_cfg.example_parameter)
                    parameter['cconst type'] = 'const ' + base_cfg.type[param]
                    parameter['ctype'] = base_cfg.type[param]
                    parameter['type'] = base_cfg.type[param]
                    parameter['min'] = base_cfg.min[param]
                    parameter['max'] = base_cfg.max[param]
                    parameter['level'] = base_cfg.level[param]
                    base_cfg.config_description['parameters'].append(parameter)
                    # TODO(lucasw) support float
                    self.subs[param] = rospy.Subscriber(prefix_feedback + param,
                            Int32, self.feedback_callback, param, queue_size=2)
                    self.pubs[param] = rospy.Publisher(prefix + param,
                            Int32, queue_size=2)

        self.dr_server = Server(base_cfg, self.dr_callback)

    def dr_callback(self, config, level):
        rospy.loginfo("""Reconfigure Request: {int_param}, {double_param},\ 
                      {str_param}, {bool_param}, {size}""".format(**config))
        # TODO(lucasw) publish on all the topics that have been updated in dr

    def feedback_callback(self, msg, param):
        rospy.loginfo(param + " " + msg.data)
        # TODO(lucasw) update the dr server
        # self.pubs[name].publish(int(value))

if __name__ == "__main__":
    rospy.init_node("dr_topics")
    dr_topics = DrTopics()