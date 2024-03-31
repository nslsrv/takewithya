# coding: utf-8

import re
import sys
import os
import logging
import fnmatch
import json
import time
import collections
import py
import pytest
import _pytest

import yatest_tools as tools
import yatest_lib.tools

import yatest_lib.external as canon

console_logger = logging.getLogger("console")
yatest_logger = logging.getLogger("ya.test")


_pytest.main.EXIT_NOTESTSCOLLECTED = 0


def configure_pdb_on_demand():
    import signal

    if hasattr(signal, "SIGUSR1"):
        def on_signal(*args):
            import pdb
            pdb.set_trace()

        signal.signal(signal.SIGUSR1, on_signal)


class TestMisconfigurationException(Exception):
    pass


class CustomImporter(object):
    def __init__(self, roots):
        self._roots = roots

    def find_module(self, fullname, package_path=None):
        for path in self._roots:
            full_path = self._get_module_path(path, fullname)

            if os.path.exists(full_path) and os.path.isdir(full_path) and not os.path.exists(os.path.join(full_path, "__init__.py")):
                open(os.path.join(full_path, "__init__.py"), "w").close()

        return None

    def _get_module_path(self, path, fullname):
        return os.path.join(path, *fullname.split('.'))


class RunMode(object):
    Run = "run"
    List = "list"


class YaTestLoggingFileHandler(logging.FileHandler):
    pass


def setup_logging(log_path, level=logging.DEBUG, *other_logs):
    logs = [log_path] + list(other_logs)
    root_logger = logging.getLogger()
    for i in range(len(root_logger.handlers) - 1, -1, -1):
        if isinstance(root_logger.handlers[i], YaTestLoggingFileHandler):
            root_logger.handlers.pop(i)
    root_logger.setLevel(level)
    for log_file in logs:
        file_handler = YaTestLoggingFileHandler(log_file)
        log_format = '%(asctime)s - %(levelname)s - %(name)s - %(funcName)s: %(message)s'
        file_handler.setFormatter(logging.Formatter(log_format))
        file_handler.setLevel(level)
        root_logger.addHandler(file_handler)


def pytest_addoption(parser):
    parser.addoption("--build-root", action="store", dest="build_root", default="", help="path to the build root")
    parser.addoption("--dep-root", action="append", dest="dep_roots", default=[], help="path to the dep build roots")
    parser.addoption("--source-root", action="store", dest="source_root", default="", help="path to the source root")
    parser.addoption("--data-root", action="store", dest="data_root", default="", help="path to the arcadia_tests_data root")
    parser.addoption("--output-dir", action="store", dest="output_dir", default="", help="path to the test output dir")
    parser.addoption("--python-path", action="store", dest="python_path", default="", help="path the canonical python binary")
    parser.addoption("--valgrind-path", action="store", dest="valgrind_path", default="", help="path the canonical valgring binary")
    parser.addoption("--test-filter", action="append", dest="test_filter", default=None, help="test filter")
    parser.addoption("--test-file-filter", action="append", dest="test_file_filter", default=None, help="test file filter")
    parser.addoption("--test-param", action="append", dest="test_params", default=None, help="test parameters")
    parser.addoption("--test-log-level", action="store", dest="test_log_level", choices=["critical", "error", "warning", "info", "debug"], default="debug", help="test log level")
    parser.addoption("--mode", action="store", choices=[RunMode.List, RunMode.Run], dest="mode", default=RunMode.Run, help="testing mode")
    parser.addoption("--modulo", default=1, type=int)
    parser.addoption("--modulo-index", default=0, type=int)
    parser.addoption("--split-by-tests", action='store_true', help="Split test execution by tests instead of suites", default=False)
    parser.addoption("--project-path", action="store", default="", help="path to CMakeList where test is declared")
    parser.addoption("--build-type", action="store", default="", help="build type")
    parser.addoption("--flags", action="append", dest="flags", default=None, help="build flags (-D)")
    parser.addoption("--sanitize", action="store", default="", help="sanitize mode")
    parser.addoption("--test-stderr", action="store_true", default=False, help="test stderr")
    parser.addoption("--test-debug", action="store_true", default=False, help="test debug mode")
    parser.addoption("--root-dir", action="store", default=None)
    parser.addoption("--ya-trace", action="store", dest="ya_trace_path", default=None, help="path to ya trace report")
    parser.addoption("--ya-version", action="store", dest="ya_version", default=0, type=int, help="allows to be compatible with ya and the new changes in ya-dev")
    parser.addoption(
        "--test-suffix", action="store", dest="test_suffix", default=None, help="add suffix to every test name"
    )
    parser.addoption("--gdb-path", action="store", dest="gdb_path", default="", help="path the canonical gdb binary")
    parser.addoption("--collect-cores", action="store_true", dest="collect_cores", default=False, help="allows core dump file recovering during test")
    parser.addoption("--sanitizer-extra-checks", action="store_true", dest="sanitizer_extra_checks", default=False, help="enables extra checks for tests built with sanitizers")
    parser.addoption("--report-deselected", action="store_true", dest="report_deselected", default=False, help="report deselected tests to the trace file")
    parser.addoption("--pdb-on-sigusr1", action="store_true", default=False, help="setup pdb.set_trace on SIGUSR1")


def pytest_configure(config):
    if pytest.__version__ != "2.7.2":
        from _pytest.monkeypatch import monkeypatch
        import reinterpret
        m = next(monkeypatch())
        m.setattr(py.builtin.builtins, 'AssertionError', reinterpret.AssertionError)  # noqa

        config.option.continue_on_collection_errors = True

    config.from_ya_test = "YA_TEST_RUNNER" in os.environ
    config.test_logs = collections.defaultdict(dict)
    config.test_metrics = {}
    context = {
        "project_path": config.option.project_path,
        "test_stderr": config.option.test_stderr,
        "test_debug": config.option.test_debug,
        "build_type": config.option.build_type,
        "test_traceback": config.option.tbstyle,
        "flags": config.option.flags,
        "sanitize": config.option.sanitize,
    }
    config.ya = Ya(
        config.option.mode,
        config.option.source_root,
        config.option.build_root,
        config.option.dep_roots,
        config.option.output_dir,
        config.option.test_params,
        context,
        config.option.python_path,
        config.option.valgrind_path,
        config.option.gdb_path,
        config.option.data_root,
    )
    config.option.test_log_level = {
        "critical": logging.CRITICAL,
        "error": logging.ERROR,
        "warning": logging.WARN,
        "info": logging.INFO,
        "debug": logging.DEBUG,
    }[config.option.test_log_level]

    if not config.option.collectonly:
        setup_logging(os.path.join(config.ya.output_dir, "run.log"), config.option.test_log_level)
    config.current_item_nodeid = None
    config.current_test_name = None
    config.test_cores_count = 0
    config.collect_cores = config.option.collect_cores
    config.sanitizer_extra_checks = config.option.sanitizer_extra_checks

    if config.sanitizer_extra_checks:
        for envvar in ['LSAN_OPTIONS', 'ASAN_OPTIONS']:
            if envvar in os.environ:
                os.environ.pop(envvar)
            if envvar + '_ORIGINAL' in os.environ:
                os.environ[envvar] = os.environ[envvar + '_ORIGINAL']

    config.coverage = None
    cov_prefix = os.environ.get('PYTHON_COVERAGE_PREFIX')
    if cov_prefix:
        config.coverage_data_dir = os.path.dirname(cov_prefix)

        import coverage
        cov = coverage.Coverage(
            data_file=cov_prefix,
            concurrency=['multiprocessing', 'thread'],
            auto_data=True,
            # debug=['pid', 'trace', 'sys', 'config'],
        )
        config.coverage = cov
        config.coverage.start()
        logging.info("Coverage will be collected during testing. pid: %d", os.getpid())

    if config.option.root_dir:
        config.rootdir = config.invocation_dir = py.path.local(config.option.root_dir)

    # Arcadia paths from the test DEPENDS section of CMakeLists.txt
    sys.path.insert(0, os.path.join(config.option.source_root, config.option.project_path))
    sys.path.extend([os.path.join(config.option.source_root, d) for d in config.option.dep_roots])
    sys.path.extend([os.path.join(config.option.build_root, d) for d in config.option.dep_roots])

    # Build root is required for correct import of protobufs, because imports are related to the root
    # (like import devtools.dummy_arcadia.protos.lib.my_proto_pb2)
    sys.path.append(config.option.build_root)
    os.environ["PYTHONPATH"] = os.pathsep.join(os.environ.get("PYTHONPATH", "").split(os.pathsep) + sys.path)

    if not config.option.collectonly:
        if config.option.ya_trace_path:
            config.ya_trace_reporter = TraceReportGenerator(config.option.ya_trace_path)
        else:
            config.ya_trace_reporter = DryTraceReportGenerator(config.option.ya_trace_path)
    config.ya_version = config.option.ya_version

    sys.meta_path.append(CustomImporter([config.option.build_root] + [os.path.join(config.option.build_root, dep) for dep in config.option.dep_roots]))
    if config.option.pdb_on_sigusr1:
        configure_pdb_on_demand()


def pytest_runtest_setup(item):
    pytest.config.test_cores_count = 0
    pytest.config.current_item_nodeid = item.nodeid
    class_name, test_name = tools.split_node_id(item.nodeid)
    test_log_path = tools.get_test_log_file_path(pytest.config.ya.output_dir, class_name, test_name)
    setup_logging(
        os.path.join(pytest.config.ya.output_dir, "run.log"),
        pytest.config.option.test_log_level,
        test_log_path
    )
    pytest.config.test_logs[item.nodeid]['log'] = test_log_path
    pytest.config.test_logs[item.nodeid]['logsdir'] = pytest.config.ya.output_dir
    pytest.config.current_test_log_path = test_log_path
    pytest.config.current_test_name = "{}::{}".format(class_name, test_name)
    separator = "#" * 100
    yatest_logger.info(separator)
    yatest_logger.info(test_name)
    yatest_logger.info(separator)
    yatest_logger.info("Test setup")

    test_item = CrashedTestItem(item.nodeid, pytest.config.option.test_suffix)
    pytest.config.ya_trace_reporter.on_start_test_class(test_item)
    pytest.config.ya_trace_reporter.on_start_test_case(test_item)


def pytest_runtest_teardown(item, nextitem):
    yatest_logger.info("Test teardown")


def pytest_runtest_call(item):
    yatest_logger.info("Test call")


def pytest_collection_modifyitems(items, config):

    def filter_items(filters):
        filtered_items = []
        deselected_items = []
        for item in items:
            canonical_node_id = str(CustomTestItem(item.nodeid, pytest.config.option.test_suffix))
            matched = False
            for flt in filters:
                if "::" not in flt and "*" not in flt:
                    flt += "*"  # add support for filtering by module name
                if canonical_node_id.endswith(flt) or fnmatch.fnmatch(tools.escape_for_fnmatch(canonical_node_id), tools.escape_for_fnmatch(flt)):
                    matched = True
            if matched:
                filtered_items.append(item)
            else:
                deselected_items.append(item)

                if config.option.report_deselected:
                    deselected_item = DeselectedTestItem(item.nodeid, config.option.test_suffix)
                    config.ya_trace_reporter.on_start_test_class(deselected_item)
                    config.ya_trace_reporter.on_start_test_case(deselected_item)
                    config.ya_trace_reporter.on_finish_test_case(deselected_item)
                    config.ya_trace_reporter.on_finish_test_class(deselected_item)

        config.hook.pytest_deselected(items=deselected_items)
        items[:] = filtered_items

    # XXX - check to be removed when tests for peerdirs don't run
    for item in items:
        if not item.nodeid:
            item._nodeid = os.path.basename(item.location[0])

    if config.option.test_file_filter:
        filter_items([f.replace("/", ".") for f in config.option.test_file_filter])

    if config.option.test_filter:
        filter_items(config.option.test_filter)

    modulo = config.option.modulo
    if modulo > 1:
        items[:] = sorted(items, key=lambda item: item.nodeid)
        modulo_index = config.option.modulo_index
        split_by_tests = config.option.split_by_tests
        items_by_classes = {}
        res = []
        for item in items:
            if "()" in item.nodeid and not split_by_tests:
                class_name = item.nodeid.split("()", 1)[0]
                if class_name not in items_by_classes:
                    items_by_classes[class_name] = []
                    res.append(items_by_classes[class_name])
                items_by_classes[class_name].append(item)
            else:
                res.append([item])

        shift = (len(res) + modulo - 1) / modulo
        start = modulo_index * shift
        end = start + shift
        chunk_items = []
        for classes_items in res[start:end]:
            chunk_items.extend(classes_items)
        items[:] = chunk_items
        yatest_logger.info("Modulo %s tests are: %s", modulo_index, chunk_items)

    if config.option.mode == RunMode.Run:
        for item in items:
            test_item = NotLaunchedTestItem(item.nodeid, config.option.test_suffix)
            config.ya_trace_reporter.on_start_test_class(test_item)
            config.ya_trace_reporter.on_start_test_case(test_item)
            config.ya_trace_reporter.on_finish_test_case(test_item)
            config.ya_trace_reporter.on_finish_test_class(test_item)
    elif config.option.mode == RunMode.List:
        tests = []
        for item in items:
            item = CustomTestItem(item.nodeid, pytest.config.option.test_suffix)
            tests.append({
                "class": item.class_name,
                "test": item.test_name,
            })
        sys.stderr.write(json.dumps(tests))


def pytest_collectreport(report):
    if not report.passed:
        if hasattr(pytest.config, 'ya_trace_reporter'):
            test_item = TestItem(report, None, pytest.config.option.test_suffix)
            pytest.config.ya_trace_reporter.on_error(test_item)
        else:
            sys.stderr.write(yatest_lib.tools.to_utf8(report.longrepr))


def pytest_runtest_makereport(item, call):

    def makereport(item, call):
        when = call.when
        duration = call.stop-call.start
        keywords = dict([(x, 1) for x in item.keywords])
        excinfo = call.excinfo
        sections = []
        if not call.excinfo:
            outcome = "passed"
            longrepr = None
        else:
            if not isinstance(excinfo, _pytest._code.code.ExceptionInfo):
                outcome = "failed"
                longrepr = excinfo
            elif excinfo.errisinstance(pytest.skip.Exception):
                outcome = "skipped"
                r = excinfo._getreprcrash()
                longrepr = (str(r.path), r.lineno, r.message)
            else:
                outcome = "failed"
                if call.when == "call":
                    longrepr = item.repr_failure(excinfo)
                else:  # exception in setup or teardown
                    longrepr = item._repr_failure_py(excinfo, style=item.config.option.tbstyle)
        for rwhen, key, content in item._report_sections:
            sections.append(("Captured std%s %s" % (key, rwhen), content))
        return _pytest.runner.TestReport(item.nodeid, item.location, keywords, outcome, longrepr, when, sections, duration)

    def logreport(report, result):
        test_item = TestItem(report, result, pytest.config.option.test_suffix)
        if report.when == "call":
            pytest.config.ya_trace_reporter.on_finish_test_case(test_item)
        elif report.when == "setup":
            pytest.config.ya_trace_reporter.on_start_test_class(test_item)
            if report.outcome != "passed":
                pytest.config.ya_trace_reporter.on_start_test_case(test_item)
                pytest.config.ya_trace_reporter.on_finish_test_case(test_item)
            else:
                pytest.config.ya_trace_reporter.on_start_test_case(test_item)
        elif report.when == "teardown":
            if report.outcome == "failed":
                pytest.config.ya_trace_reporter.on_start_test_case(test_item)
                pytest.config.ya_trace_reporter.on_finish_test_case(test_item)
            pytest.config.ya_trace_reporter.on_finish_test_class(test_item)

    rep = makereport(item, call)
    if hasattr(call, 'result') and call.result:
        result = call.result
        if not pytest.config.from_ya_test:
            ti = TestItem(rep, result, pytest.config.option.test_suffix)
            tr = pytest.config.pluginmanager.getplugin('terminalreporter')
            tr.write_line("{} - Validating canonical data is not supported when running standalone binary".format(ti), yellow=True, bold=True)
    else:
        result = None

    # taken from arcadia/contrib/python/pytest/_pytest/skipping.py
    try:
        # unitttest special case, see setting of _unexpectedsuccess
        if hasattr(item, '_unexpectedsuccess'):
            if rep.when == "call":
                # we need to translate into how pytest encodes xpass
                rep.wasxfail = "reason: " + repr(item._unexpectedsuccess)
                rep.outcome = "failed"
            return rep
        if not (call.excinfo and call.excinfo.errisinstance(pytest.xfail.Exception)):
            evalxfail = getattr(item, '_evalxfail', None)
            if not evalxfail:
                return
        if call.excinfo and call.excinfo.errisinstance(pytest.xfail.Exception):
            if not item.config.getvalue("runxfail"):
                rep.wasxfail = "reason: " + call.excinfo.value.msg
                rep.outcome = "skipped"
                return rep
        evalxfail = item._evalxfail
        if not rep.skipped:
            if not item.config.option.runxfail:
                if evalxfail.wasvalid() and evalxfail.istrue():
                    if call.excinfo:
                        if evalxfail.invalidraise(call.excinfo.value):
                            rep.outcome = "failed"
                            return rep
                        else:
                            rep.outcome = "skipped"
                    elif call.when == "call":
                        rep.outcome = "failed"
                    else:
                        return rep
                    rep.wasxfail = evalxfail.getexplanation()
                    return rep
    finally:
        logreport(rep, result)
    return rep


def get_formatted_error(report):
    if isinstance(report.longrepr, tuple):
        text = ""
        for entry in report.longrepr:
            text += colorize(entry)
    else:
        text = colorize(report.longrepr)
    text = yatest_lib.tools.to_utf8(text)
    return text


def colorize(longrepr):
    # use default pytest colorization
    if pytest.config.option.tbstyle != "short":
        io = py.io.TextIO()
        writer = py.io.TerminalWriter(file=io)
        # enable colorization
        writer.hasmarkup = True

        if hasattr(longrepr, 'reprtraceback') and hasattr(longrepr.reprtraceback, 'toterminal'):
            longrepr.reprtraceback.toterminal(writer)
            return io.getvalue().strip()
        return yatest_lib.tools.to_utf8(longrepr)

    text = yatest_lib.tools.to_utf8(longrepr)
    pos = text.find("E   ")
    if pos == -1:
        return text

    bt, error = text[:pos], text[pos:]
    filters = [
        # File path, line number and function name
        (re.compile(r"^(.*?):(\d+): in (\S+)", flags=re.MULTILINE), r"[[unimp]]\1[[rst]]:[[alt2]]\2[[rst]]: in [[alt1]]\3[[rst]]"),
    ]
    for regex, substitution in filters:
        bt = regex.sub(substitution, bt)
    return "{}[[bad]]{}".format(bt, error)


class TestItem(object):

    def __init__(self, report, result, test_suffix):
        self._result = result
        self.nodeid = report.nodeid
        self._class_name, self._test_name = tools.split_node_id(self.nodeid, test_suffix)
        self._error = None
        self._status = None
        self._process_report(report)
        self._duration = hasattr(report, 'duration') and report.duration or 0
        self._keywords = getattr(report, "keywords", {})

    def _process_report(self, report):
        if report.longrepr:
            self.set_error(report)
            if hasattr(report, 'when') and report.when != "call":
                self.set_error(report.when + " failed:\n" + self._error)
        else:
            self.set_error("")
        if report.outcome == "passed":
            self._status = 'good'
            self.set_error("")
        elif report.outcome == "skipped":
            if hasattr(report, 'wasxfail'):
                self._status = 'xfail'
                self.set_error(report.wasxfail)
            else:
                self._status = 'skipped'
                self.set_error(yatest_lib.tools.to_utf8(report.longrepr[-1]))
        elif report.outcome == "failed":
            if hasattr(report, 'wasxfail'):
                self._status = 'xpass'
                self.set_error("Test unexpectedly passed")
            else:
                self._status = 'fail'

    @property
    def status(self):
        return self._status

    def set_status(self, status):
        self._status = status

    @property
    def test_name(self):
        return tools.normalize_name(self._test_name)

    @property
    def class_name(self):
        return tools.normalize_name(self._class_name)

    @property
    def error(self):
        return self._error

    def set_error(self, entry):
        if isinstance(entry, _pytest.runner.BaseReport):
            self._error = get_formatted_error(entry)
        else:
            self._error = "[[bad]]" + str(entry)

    @property
    def duration(self):
        return self._duration

    @property
    def result(self):
        if 'not_canonize' in self._keywords:
            return None
        return self._result

    @property
    def keywords(self):
        return self._keywords

    def __str__(self):
        return "{}::{}".format(self.class_name, self.test_name)


class CustomTestItem(TestItem):

    def __init__(self, nodeid, test_suffix):
        self._result = None
        self.nodeid = nodeid
        self._class_name, self._test_name = tools.split_node_id(nodeid, test_suffix)
        self._duration = 0
        self._error = ""
        self._keywords = {}


class NotLaunchedTestItem(CustomTestItem):

    def __init__(self, nodeid, test_suffix):
        super(NotLaunchedTestItem, self).__init__(nodeid, test_suffix)
        self._status = "not_launched"


class CrashedTestItem(CustomTestItem):

    def __init__(self, nodeid, test_suffix):
        super(CrashedTestItem, self).__init__(nodeid, test_suffix)
        self._status = "crashed"


class DeselectedTestItem(CustomTestItem):

    def __init__(self, nodeid, test_suffix):
        super(DeselectedTestItem, self).__init__(nodeid, test_suffix)
        self._status = "deselected"


class TraceReportGenerator(object):

    def __init__(self, out_file_path):
        self.File = open(out_file_path, 'w')

    def on_start_test_class(self, test_item):
        self.trace('test-started', {'class': test_item.class_name.decode('utf-8')})

    def on_finish_test_class(self, test_item):
        self.trace('test-finished', {'class': test_item.class_name.decode('utf-8')})

    def on_start_test_case(self, test_item):
        message = {
            'class': yatest_lib.tools.to_utf8(test_item.class_name),
            'subtest': yatest_lib.tools.to_utf8(test_item.test_name)
        }
        if test_item.nodeid in pytest.config.test_logs:
            message['logs'] = pytest.config.test_logs[test_item.nodeid]
        self.trace('subtest-started', message)

    def on_finish_test_case(self, test_item):
        if test_item.result:
            try:
                result = canon.serialize(test_item.result[0])
            except Exception as e:
                yatest_logger.exception("Error while serializing test results")
                test_item.set_error("Invalid test result: {}".format(e))
                test_item.set_status("fail")
                result = None
        else:
            result = None
        message = {
            'class': yatest_lib.tools.to_utf8(test_item.class_name),
            'subtest': yatest_lib.tools.to_utf8(test_item.test_name),
            'status': test_item.status,
            'comment': self._get_comment(test_item),
            'time': test_item.duration,
            'result': result,
            'metrics': pytest.config.test_metrics.get(test_item.nodeid),
            'is_diff_test': 'diff_test' in test_item.keywords,
        }
        if test_item.nodeid in pytest.config.test_logs:
            message['logs'] = pytest.config.test_logs[test_item.nodeid]
        self.trace('subtest-finished', message)

    def on_error(self, test_item):
        self.trace('suite_event', {"errors": [(test_item.status, self._get_comment(test_item))]})

    @staticmethod
    def _get_comment(test_item):
        msg = yatest_lib.tools.to_utf8(test_item.error)
        if not msg:
            return ""
        return msg + "[[rst]]"

    def trace(self, name, value):
        event = {
            'timestamp': time.time(),
            'value': value,
            'name': name
        }
        self.File.write(json.dumps(event, ensure_ascii=False) + '\n')
        self.File.flush()


class DryTraceReportGenerator(TraceReportGenerator):
    """
    Generator does not write any information.
    """

    def __init__(self, *args, **kwargs):
        pass

    def trace(self, name, value):
        pass


class Ya(object):
    """
    Adds integration with ya, helps in finding dependencies
    """

    def __init__(self, mode, source_root, build_root, dep_roots, output_dir, test_params, context, python_path, valgrind_path, gdb_path, data_root):
        self._mode = mode
        self._build_root = build_root
        self._source_root = source_root or self._detect_source_root()
        self._output_dir = output_dir or self._detect_output_root()

        if not self._output_dir:
            raise Exception("Run ya make -t before running test binary")
        if not self._source_root:
            logging.warning("Source root was not set neither determined, use --source-root to set it explicitly")
        if not self._build_root:
            if self._source_root:
                self._build_root = self._source_root
            else:
                logging.warning("Build root was not set neither determined, use --build-root to set it explicitly")

        if data_root:
            self._data_root = data_root
        elif self._source_root:
            self._data_root = os.path.abspath(os.path.join(self._source_root, "..", "arcadia_tests_data"))

        self._dep_roots = dep_roots

        self._python_path = python_path
        self._valgrind_path = valgrind_path
        self._gdb_path = gdb_path
        self._test_params = {}
        self._context = {}
        self._test_metrics = {}
        if test_params:
            for p in test_params:
                k, v = p.split("=", 1)
                self._test_params[k] = v
        self._context.update(context)

    @property
    def source_root(self):
        return self._source_root

    @property
    def data_root(self):
        return self._data_root

    @property
    def build_root(self):
        return self._build_root

    @property
    def dep_roots(self):
        return self._dep_roots

    @property
    def output_dir(self):
        return self._output_dir

    @property
    def python_path(self):
        return self._python_path or sys.executable

    @property
    def valgrind_path(self):
        if not self._valgrind_path:
            raise ValueError("path to valgrind was not pass correctly, use --valgrind-path to fix it")
        return self._valgrind_path

    @property
    def gdb_path(self):
        return self._gdb_path

    def get_binary(self, *path):
        assert self._build_root, "Build root was not set neither determined, use --build-root to set it explicitly"
        path = list(path)
        if os.name == "nt":
            if not path[-1].endswith(".exe"):
                path[-1] += ".exe"
        for binary_path in [os.path.join(self.build_root, "bin", *path), os.path.join(self.build_root, *path)]:
            if os.path.exists(binary_path):
                yatest_logger.debug("Binary was found by %s", binary_path)
                return binary_path
            yatest_logger.debug("%s not found", binary_path)
        error_message = "Cannot find binary '{binary}': make sure it was added in the DEPENDS section".format(binary=path)
        yatest_logger.debug(error_message)
        if self._mode == RunMode.Run:
            raise TestMisconfigurationException(error_message)

    def file(self, path, diff_tool=None, local=False, diff_file_name=None, diff_tool_timeout=None):
        return canon.ExternalDataInfo.serialize_file(path, diff_tool=diff_tool, local=local, diff_file_name=diff_file_name, diff_tool_timeout=diff_tool_timeout)

    def get_param(self, key, default=None):
        return self._test_params.get(key, default)

    def get_param_dict_copy(self):
        return dict(self._test_params)

    def get_context(self, key):
        return self._context[key]

    def _detect_source_root(self):
        root = None
        try:
            import library.python.find_root
            # try to determine source root from cwd
            cwd = os.getcwd()
            root = library.python.find_root.detect_root(cwd)

            if not root:
                # try to determine root pretending we are in the test work dir made from --keep-temps run
                env_subdir = os.path.join("environment", "arcadia")
                root = library.python.find_root.detect_root(cwd, detector=lambda p: os.path.exists(os.path.join(p, env_subdir)))
        except ImportError:
            logging.warning("Unable to import library.python.find_root")

        return root

    def _detect_output_root(self):
        for p in [
            # if run from kept test working dir
            tools.TESTING_OUT_DIR_NAME,
            # if run from source dir
            os.path.join("test-results", os.path.basename(os.path.splitext(sys.argv[0])[0]), tools.TESTING_OUT_DIR_NAME),
        ]:
            if os.path.exists(p):
                return p
        return None
