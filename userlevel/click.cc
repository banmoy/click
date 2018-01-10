// -*- c-basic-offset: 4 -*-
/*
 * click.cc -- user-level Click main program
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 * Copyright (c) 2001-2003 International Computer Science Institute
 * Copyright (c) 2004-2006 Regents of the University of California
 * Copyright (c) 2008-2009 Meraki, Inc.
 * Copyright (c) 1999-2015 Eddie Kohler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/pathvars.h>

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#if HAVE_EXECINFO_H
# include <execinfo.h>
#endif

#ifdef HAVE_DPDK
# include <rte_common.h>
# include <rte_eal.h>
# include <rte_lcore.h>
#endif // HAVE_DPDK
#include <click/lexer.hh>
#include <click/routerthread.hh>
#include <click/router.hh>
#include <click/master.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <click/straccum.hh>
#include <click/clp.h>
#include <click/archive.hh>
#include <click/glue.hh>
#include <click/driver.hh>
#include <click/userutils.hh>
#include <click/args.hh>
#include <click/handlercall.hh>
#include <click/msgqueue.hh>
#include "elements/standard/quitwatcher.hh"
#include "elements/userlevel/controlsocket.hh"
CLICK_USING_DECLS

#define HELP_OPT                300
#define VERSION_OPT             301
#define CLICKPATH_OPT           302
#define ROUTER_OPT              303
#define EXPRESSION_OPT          304
#define QUIT_OPT                305
#define OUTPUT_OPT              306
#define HANDLER_OPT             307
#define TIME_OPT                308
#define PORT_OPT                310
#define UNIX_SOCKET_OPT         311
#define NO_WARNINGS_OPT         312
#define WARNINGS_OPT            313
#define ALLOW_RECONFIG_OPT      314
#define EXIT_HANDLER_OPT        315
#define THREADS_OPT             316
#define SIMTIME_OPT             317
#define SOCKET_OPT              318
#define THREADS_AFF_OPT         319
#define DPDK_OPT                320

static const Clp_Option options[] = {
    { "allow-reconfigure", 'R', ALLOW_RECONFIG_OPT, 0, Clp_Negate },
    { "clickpath", 'C', CLICKPATH_OPT, Clp_ValString, 0 },
    { "expression", 'e', EXPRESSION_OPT, Clp_ValString, 0 },
    { "dpdk", 0, DPDK_OPT, 0, 0 },
    { "file", 'f', ROUTER_OPT, Clp_ValString, 0 },
    { "handler", 'h', HANDLER_OPT, Clp_ValString, 0 },
    { "help", 0, HELP_OPT, 0, 0 },
    { "output", 'o', OUTPUT_OPT, Clp_ValString, 0 },
    { "socket", 0, SOCKET_OPT, Clp_ValInt, 0 },
    { "port", 'p', PORT_OPT, Clp_ValString, 0 },
    { "quit", 'q', QUIT_OPT, 0, 0 },
    { "simtime", 0, SIMTIME_OPT, Clp_ValDouble, Clp_Optional },
    { "simulation-time", 0, SIMTIME_OPT, Clp_ValDouble, Clp_Optional },
    { "threads", 'j', THREADS_OPT, Clp_ValInt, 0 },
    { "cpu", 0, THREADS_AFF_OPT, Clp_ValInt, Clp_Optional | Clp_Negate },
    { "affinity", 'a', THREADS_AFF_OPT, Clp_ValInt, Clp_Optional | Clp_Negate },
    { "time", 't', TIME_OPT, 0, 0 },
    { "unix-socket", 'u', UNIX_SOCKET_OPT, Clp_ValString, 0 },
    { "version", 'v', VERSION_OPT, 0, 0 },
    { "warnings", 0, WARNINGS_OPT, 0, Clp_Negate },
    { "exit-handler", 'x', EXIT_HANDLER_OPT, Clp_ValString, 0 },
    { 0, 'w', NO_WARNINGS_OPT, 0, Clp_Negate },
};

static const char *program_name;

void
short_usage()
{
  fprintf(stderr, "Usage: %s [OPTION]... [ROUTERFILE]\n\
Try '%s --help' for more information.\n",
          program_name, program_name);
}

void
usage()
{
    printf("\
'Click' runs a Click router configuration at user level. It installs the\n\
configuration, reporting any errors to standard error, and then generally runs\n\
until interrupted.\n\
\n\
Usage: %s [OPTION]... [ROUTERFILE]\n\
\n\
Options:\n\
  -f, --file FILE               Read router configuration from FILE.\n\
  -e, --expression EXPR         Use EXPR as router configuration.\n\
  -j, --threads N               Start N threads (default 1).\n", program_name);
#if HAVE_DPDK
    printf("\
      --dpdk DPDK_ARGS --       Enable DPDK and give DPDK's own arguments.\n");
#endif
#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
    printf("\
  -a, --affinity[=N]            Pin threads to CPUs starting at #N (default 0).\n");
#endif
    printf("\
  -p, --port PORT               Listen for control connections on TCP port.\n\
  -u, --unix-socket FILE        Listen for control connections on Unix socket.\n\
      --socket FD               Add a file descriptor control connection.\n\
  -R, --allow-reconfigure       Provide a writable 'hotconfig' handler.\n\
  -h, --handler ELEMENT.H       Call ELEMENT's read handler H after running\n\
                                driver and print result to standard output.\n\
  -x, --exit-handler ELEMENT.H  Use handler ELEMENT.H value for exit status.\n\
  -o, --output FILE             Write flat configuration to FILE.\n\
  -q, --quit                    Do not run driver.\n\
  -t, --time                    Print information on how long driver took.\n\
  -w, --no-warnings             Do not print warnings.\n\
      --simtime                 Run in simulation time.\n\
  -C, --clickpath PATH          Use PATH for CLICKPATH.\n\
      --help                    Print this message and exit.\n\
  -v, --version                 Print version number and exit.\n\
\n\
Report bugs to <click@librelist.com>.\n");
}

static Master* click_master;
static MsgQueue* click_msgq;
static Router* click_router;
static ErrorHandler* errh;
static bool running = false;

extern "C" {
static void
stop_signal_handler(int sig)
{
#if !HAVE_SIGACTION
    signal(sig, SIG_DFL);
#endif
    if (!running)
        kill(getpid(), sig);
    else
        click_router->set_runcount(Router::STOP_RUNCOUNT);
}
}


// report handler results

static int
call_read_handler(Element *e, String handler_name,
                  bool print_name, ErrorHandler *errh)
{
  const Handler *rh = Router::handler(e, handler_name);
  String full_name = Handler::unparse_name(e, handler_name);
  if (!rh || !rh->visible())
    return errh->error("no %<%s%> handler", full_name.c_str());
  else if (!rh->read_visible())
    return errh->error("%<%s%> is a write handler", full_name.c_str());

  if (print_name)
    fprintf(stdout, "%s:\n", full_name.c_str());
  String result = rh->call_read(e);
  if (!rh->raw() && result && result.back() != '\n')
      result += '\n';
  fputs(result.c_str(), stdout);
  if (print_name)
    fputs("\n", stdout);

  return 0;
}

static int
expand_handler_elements(const String& pattern, const String& handler_name,
                        Vector<Element*>& elements, Router* router)
{
    // first try element name
    if (Element* e = router->find(pattern)) {
        elements.push_back(e);
        return 1;
    }
    // check if we have a pattern
    bool is_pattern = false;
    for (const char* s = pattern.begin(); s < pattern.end(); s++)
        if (*s == '?' || *s == '*' || *s == '[') {
            is_pattern = true;
            break;
        }
    // check pattern or type
    bool any = false;
    for (int i = 0; i < router->nelements(); i++)
        if (is_pattern
            ? glob_match(router->ename(i), pattern)
            : router->element(i)->cast(pattern.c_str()) != 0) {
            any = true;
            const Handler* h = Router::handler(router->element(i), handler_name);
            if (h && h->read_visible())
                elements.push_back(router->element(i));
        }
    if (!any)
        return errh->error((is_pattern ? "no element matching %<%s%>" : "no element %<%s%>"), pattern.c_str());
    else
        return 2;
}

static int
call_read_handlers(Vector<String> &handlers, ErrorHandler *errh)
{
    Vector<Element *> handler_elements;
    Vector<String> handler_names;
    bool print_names = (handlers.size() > 1);
    int before = errh->nerrors();

    // expand handler names
    for (int i = 0; i < handlers.size(); i++) {
        const char *dot = find(handlers[i], '.');
        if (dot == handlers[i].end()) {
            call_read_handler(click_router->root_element(), handlers[i],
                              print_names, errh);
            continue;
        }

        String element_name = handlers[i].substring(handlers[i].begin(), dot);
        String handler_name = handlers[i].substring(dot + 1, handlers[i].end());

        Vector<Element*> elements;
        int retval = expand_handler_elements(element_name, handler_name,
                                             elements, click_router);
        if (retval >= 0)
            for (int j = 0; j < elements.size(); j++)
                call_read_handler(elements[j], handler_name,
                                  print_names || retval > 1, errh);
    }

    return (errh->nerrors() == before ? 0 : -1);
}


// hotswapping

static Router* hotswap_router;
static Router* hotswap_thunk_router;
static bool hotswap_hook(Task *, void *);
static Task hotswap_task(hotswap_hook, 0);

static bool
hotswap_hook(Task*, void*)
{
    hotswap_thunk_router->set_foreground(false);
    hotswap_router->activate(ErrorHandler::default_handler());
    click_router->unuse();
    click_router = hotswap_router;
    click_router->use();
    hotswap_router = 0;
    return true;
}

// switching configurations

static Vector<String> cs_unix_sockets;
static Vector<String> cs_ports;
static Vector<String> cs_sockets;
static bool warnings = true;
int click_nthreads = 1;
bool dpdk_enabled = false;

static String
click_driver_control_socket_name(int number)
{
    if (!number)
        return "click_driver@@ControlSocket";
    else
        return "click_driver@@ControlSocket@" + String(number);
}

#if HAVE_MULTITHREAD
extern "C" {
static void *thread_driver(void *user_data)
{
    RouterThread *thread = static_cast<RouterThread *>(user_data);
    thread->driver();
    return 0;
}

static void *thread_cmd_driver(void *user_data) {
  RouterThread *thread = static_cast<RouterThread*>(user_data);
  thread->cmd_driver();
}

# if HAVE_DPDK
static int thread_driver_dpdk(void *user_data) {
    RouterThread *thread = static_cast<RouterThread *>(user_data);
    thread->driver();
    return 0;
}
# endif
}
#endif

static int
cleanup(Clp_Parser *clp, int exit_value)
{
    Clp_DeleteParser(clp);
    click_static_cleanup();
    delete click_master;
    return exit_value;
}

#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
static int click_affinity_offset = -1;
void do_set_affinity(pthread_t p, int cpu) {
    if (!dpdk_enabled && click_affinity_offset >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu + click_affinity_offset, &set);
        pthread_setaffinity_np(p, sizeof(cpu_set_t), &set);
    }
}
#else
# define do_set_affinity(p, cpu) /* nothing */
#endif

static Router*
create_control_router(ErrorHandler* errh) {
    int before_errors = errh->nerrors();
    Router *router = new Router("", click_master);

    if (!router)
        return 0;

    // add new ControlSockets
    int ncs = 0;
    for (String *it = cs_ports.begin(); it != cs_ports.end(); ++it, ++ncs)
        router->add_element(new ControlSocket(click_msgq), click_driver_control_socket_name(ncs), "TCP, " + *it, "click", 0);

    // catch control-C and SIGTERM
    click_signal(SIGINT, stop_signal_handler, true);
    click_signal(SIGTERM, stop_signal_handler, true);
    // ignore SIGPIPE
    click_signal(SIGPIPE, SIG_IGN, false);

  if (errh->nerrors() == before_errors
      && router->initialize(errh) >= 0)
    return router;
  else {
    delete router;
    return 0;
  }
}

int
main(int argc, char **argv)
{
  click_static_initialize();
  errh = ErrorHandler::default_handler();

  // read command line arguments
  Clp_Parser *clp =
    Clp_NewParser(argc, argv, sizeof(options) / sizeof(options[0]), options);
  program_name = Clp_ProgramName(clp);

  const char *router_file = 0;
  bool file_is_expr = false;
  const char *output_file = 0;
  bool quit_immediately = false;
  Vector<String> handlers;
  String exit_handler;
  Vector<char*> dpdk_arg;

  while (1) {
    int opt = Clp_Next(clp);
    switch (opt) {

     case ROUTER_OPT:
     case EXPRESSION_OPT:
     router_file:
      if (router_file) {
        errh->error("router configuration specified twice");
        goto bad_option;
      }
      router_file = clp->vstr;
      file_is_expr = (opt == EXPRESSION_OPT);
      break;

     case Clp_NotOption:
      for (const char *s = clp->vstr; *s; s++)
          if (*s == '=' && s > clp->vstr) {
              if (!click_lexer()->global_scope().define(String(clp->vstr, s), s + 1, false))
                  errh->error("parameter %<%.*s%> multiply defined", s - clp->vstr, clp->vstr);
              goto next_argument;
          } else if (!isalnum((unsigned char) *s) && *s != '_')
              break;
      goto router_file;

     case OUTPUT_OPT:
      if (output_file) {
        errh->error("output file specified twice");
        goto bad_option;
      }
      output_file = clp->vstr;
      break;

     case HANDLER_OPT:
      handlers.push_back(clp->vstr);
      break;

     case EXIT_HANDLER_OPT:
      if (exit_handler) {
        errh->error("--exit-handler specified twice");
        goto bad_option;
      }
      exit_handler = clp->vstr;
      break;

  case PORT_OPT: {
      uint16_t portno;
      int portno_int = -1;
      String vstr(clp->vstr);
      if (IPPortArg(IP_PROTO_TCP).parse(vstr, portno))
          cs_ports.push_back(String(portno));
      else if (vstr && vstr.back() == '+'
               && IntArg().parse(vstr.substring(0, -1), portno_int)
               && portno_int > 0 && portno_int < 65536)
          cs_ports.push_back(String(portno_int) + "+");
      else {
          Clp_OptionError(clp, "%<%O%> expects a TCP port number, not %<%s%>", clp->vstr);
          goto bad_option;
      }
      break;
  }

     case UNIX_SOCKET_OPT:
      cs_unix_sockets.push_back(clp->vstr);
      break;

    case SOCKET_OPT:
        cs_sockets.push_back(clp->vstr);
        break;

     case QUIT_OPT:
      quit_immediately = true;
      break;

     case WARNINGS_OPT:
      warnings = !clp->negated;
      break;

     case NO_WARNINGS_OPT:
      warnings = clp->negated;
      break;
#if HAVE_DPDK
     case DPDK_OPT: {
      const char* arg;
      dpdk_arg.push_back(argv[0]);
      do {
        arg = Clp_Shift(clp, 1);
        if (arg == NULL) break;
        dpdk_arg.push_back(const_cast<char*>(arg));
      } while (strcmp(arg, "--") != 0);
      dpdk_enabled = true;
      break;
     }
#endif // HAVE_DPDK
     case THREADS_OPT:
      click_nthreads = clp->val.i;
      if (click_nthreads <= 1)
          click_nthreads = 1;
#if !HAVE_MULTITHREAD
      if (click_nthreads > 1) {
          errh->warning("Click was built without multithread support, running single threaded");
          click_nthreads = 1;
      }
#endif
      break;

     case THREADS_AFF_OPT:
#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
      if (clp->negated)
          click_affinity_offset = -1;
      else if (clp->have_val)
          click_affinity_offset = clp->val.i;
      else
          click_affinity_offset = 0;
#else
      errh->warning("CPU affinity is not supported on this platform");
#endif
      break;

    case SIMTIME_OPT: {
        Timestamp::warp_set_class(Timestamp::warp_simulation);
        Timestamp simbegin(clp->have_val ? clp->val.d : 1000000000);
        Timestamp::warp_set_now(simbegin, simbegin);
        break;
    }

     case CLICKPATH_OPT:
      set_clickpath(clp->vstr);
      break;

     case HELP_OPT:
      usage();
      return cleanup(clp, 0);

     case VERSION_OPT:
      printf("click (Click) %s\n", CLICK_VERSION);
      printf("Copyright (C) 1999-2001 Massachusetts Institute of Technology\n\
Copyright (C) 2001-2003 International Computer Science Institute\n\
Copyright (C) 2008-2009 Meraki, Inc.\n\
Copyright (C) 2004-2011 Regents of the University of California\n\
Copyright (C) 1999-2012 Eddie Kohler\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose.\n");
      return cleanup(clp, 0);

     bad_option:
     case Clp_BadOption:
      short_usage();
      return cleanup(clp, 1);

     case Clp_Done:
      goto done;

    }
   next_argument: ;
  }

 done:
#if HAVE_DPDK
    if (dpdk_enabled) {
        if (click_nthreads > 1)
            errh->warning("In DPDK mode, set the number of cores with DPDK EAL arguments");
# if HAVE_DECL_PTHREAD_SETAFFINITY_NP
        if (click_affinity_offset >= 0)
            errh->warning("In DPDK mode, set core affinity with DPDK EAL arguments");
# endif
        int n_eal_args = rte_eal_init(dpdk_arg.size(), dpdk_arg.data());
        if (n_eal_args < 0)
            rte_exit(EXIT_FAILURE,
                     "Click was built with Intel DPDK support but there was an\n"
                     "          error parsing the EAL arguments.\n");
        click_nthreads = rte_lcore_count();
    }
#endif

  // parse configuration
  click_master = new Master(click_nthreads);
  click_msgq = new MsgQueue();
  click_router = create_control_router(errh);
  if (!click_router)
    return cleanup(clp, 1);
  click_router->use();

  click_master->set_control_router(click_router);

  int exit_value = 0;
#if (HAVE_MULTITHREAD)
  Vector<pthread_t> other_threads;
#endif

  // run driver
  // 10.Apr.2004 - Don't run the router if it has no elements.
  if (!quit_immediately && click_router->nelements()) {
    running = true;
    click_router->activate(errh);
    for (int t = 0; t < click_nthreads; ++t) {
        click_master->thread(t)->mark_driver_entry();
        click_master->thread(t)->set_msgqueue(click_msgq);
    }
#if HAVE_MULTITHREAD
    {
        for (int t = 1; t < click_nthreads; ++t) {
            pthread_t p;
            pthread_create(&p, 0, thread_cmd_driver, click_master->thread(t));
            other_threads.push_back(p);
            do_set_affinity(p, t);
        }
        do_set_affinity(pthread_self(), 0);
    }
#endif

    // run driver
    click_master->thread(0)->driver();

    // now that the driver has stopped, SIGINT gets default handling
    running = false;
    click_fence();
  } else if (!quit_immediately && warnings)
    errh->warning("%s: configuration has no elements, exiting", filename_landmark(router_file, file_is_expr));

  // call handlers
  if (handlers.size())
    if (call_read_handlers(handlers, errh) < 0)
      exit_value = 1;

  // call exit handler
  if (exit_handler) {
    int before = errh->nerrors();
    String exit_string = HandlerCall::call_read(exit_handler, click_router->root_element(), errh);
    bool b;
    if (errh->nerrors() != before)
      exit_value = -1;
    else if (IntArg().parse(cp_uncomment(exit_string), exit_value))
      /* nada */;
    else if (BoolArg().parse(cp_uncomment(exit_string), b))
      exit_value = (b ? 0 : 1);
    else {
      errh->error("exit handler value should be integer");
      exit_value = -1;
    }
  }

#if HAVE_MULTITHREAD
  for (int i = 0; i < other_threads.size(); ++i)
      click_master->thread(i + 1)->wake();
  for (int i = 0; i < other_threads.size(); ++i)
      (void) pthread_join(other_threads[i], 0);
#endif

#if HAVE_MULTITHREAD && HAVE_DPDK
click_cleanup:
#endif
  click_router->unuse();
  return cleanup(clp, exit_value);
}
