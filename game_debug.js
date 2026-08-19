// include: shell.js
// include: minimum_runtime_check.js
(function() {
  // "30.0.0" -> 300000
  function humanReadableVersionToPacked(str) {
    str = str.split('-')[0]; // Remove any trailing part from e.g. "12.53.3-alpha"
    var vers = str.split('.').slice(0, 3);
    while(vers.length < 3) vers.push('00');
    vers = vers.map((n, i, arr) => n.padStart(2, '0'));
    return vers.join('');
  }
  // 300000 -> "30.0.0"
  var packedVersionToHumanReadable = n => [n / 10000 | 0, (n / 100 | 0) % 100, n % 100].join('.');

  var TARGET_NOT_SUPPORTED = 2147483647;

  // Note: We use a typeof check here instead of optional chaining using
  // globalThis because older browsers might not have globalThis defined.

  // We skip the node version checking when running on Bun/Deno since the node
  // version they report doesn't seem to be useful.
  if (typeof process !== 'undefined' && !process.versions?.bun && typeof Deno == "undefined") {
    var currentNodeVersion = process.versions?.node ? humanReadableVersionToPacked(process.versions.node) : TARGET_NOT_SUPPORTED;
    if (currentNodeVersion < 180300) {
      throw new Error(`This emscripten-generated code requires node v${ packedVersionToHumanReadable(180300) } (detected v${packedVersionToHumanReadable(currentNodeVersion)})`);
    }
  }

  var userAgent = typeof navigator !== 'undefined' && navigator.userAgent;
  if (!userAgent) {
    return;
  }

  var currentSafariVersion = userAgent.includes("Safari/") && !userAgent.includes("Chrome/") && userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/) ? humanReadableVersionToPacked(userAgent.match(/Version\/(\d+\.?\d*\.?\d*)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentSafariVersion < 150000) {
    throw new Error(`This emscripten-generated code requires Safari v${ packedVersionToHumanReadable(150000) } (detected v${currentSafariVersion})`);
  }

  var currentFirefoxVersion = userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Firefox\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentFirefoxVersion < 79) {
    throw new Error(`This emscripten-generated code requires Firefox v79 (detected v${currentFirefoxVersion})`);
  }

  var currentChromeVersion = userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/) ? parseFloat(userAgent.match(/Chrome\/(\d+(?:\.\d+)?)/)[1]) : TARGET_NOT_SUPPORTED;
  if (currentChromeVersion < 85) {
    throw new Error(`This emscripten-generated code requires Chrome v85 (detected v${currentChromeVersion})`);
  }
})();

// end include: minimum_runtime_check.js
// The Module object: Our interface to the outside world. We import
// and export values on it. There are various ways Module can be used:
// 1. Not defined. We create it here
// 2. A function parameter, function(moduleArg) => Promise<Module>
// 3. pre-run appended it, var Module = {}; ..generated code..
// 4. External script tag defines var Module.
// We need to check if Module already exists (e.g. case 3 above).
// Substitution will be replaced with actual code on later stage of the build,
// this way Closure Compiler will not mangle it (e.g. case 4. above).
// Note that if you want to run closure, and also to use Module
// after the generated code, you will need to define   var Module = {};
// before the code. Then that object will be used in the code, and you
// can continue to use Module afterwards as well.
var Module = typeof Module != 'undefined' ? Module : {};

// Determine the runtime environment we are in. You can customize this by
// setting the ENVIRONMENT setting at compile time (see settings.js).

// Attempt to auto-detect the environment
var ENVIRONMENT_IS_WEB = !!globalThis.window;
var ENVIRONMENT_IS_WORKER = !!globalThis.WorkerGlobalScope;
// N.b. Electron.js environment is simultaneously a NODE-environment, but
// also a web environment.
var ENVIRONMENT_IS_NODE = globalThis.process?.versions?.node && globalThis.process?.type != 'renderer';
var ENVIRONMENT_IS_SHELL = !ENVIRONMENT_IS_WEB && !ENVIRONMENT_IS_NODE && !ENVIRONMENT_IS_WORKER;

// --pre-jses are emitted after the Module integration code, so that they can
// refer to Module (if they choose; they can also define Module)


var programArgs = [];
var thisProgram = './this.program';
var quit_ = (status, toThrow) => {
  throw toThrow;
};

// In MODULARIZE mode _scriptName needs to be captured already at the very top of the page immediately when the page is parsed, so it is generated there
// before the page load. In non-MODULARIZE modes generate it here.
var _scriptName = globalThis.document?.currentScript?.src;

if (typeof __filename != 'undefined') { // Node
  _scriptName = __filename;
} else
if (ENVIRONMENT_IS_WORKER) {
  _scriptName = self.location.href;
}

// `/` should be present at the end if `scriptDirectory` is not empty
var scriptDirectory = '';
function locateFile(path) {
  if (Module['locateFile']) {
    return Module['locateFile'](path, scriptDirectory);
  }
  return scriptDirectory + path;
}

// Hooks that are implemented differently in different runtime environments.
var readAsync, readBinary;

if (ENVIRONMENT_IS_NODE) {
  const isNode = globalThis.process?.versions?.node && globalThis.process?.type != 'renderer';
  if (!isNode) throw new Error('not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)');

  // These modules will usually be used on Node.js. Load them eagerly to avoid
  // the complexity of lazy-loading.
  var fs = require('node:fs');

  scriptDirectory = __dirname + '/';

// include: node_shell_read.js
readBinary = (filename) => {
  // We need to re-wrap `file://` strings to URLs.
  filename = isFileURI(filename) ? new URL(filename) : filename;
  var ret = fs.readFileSync(filename);
  assert(Buffer.isBuffer(ret));
  return ret;
};

readAsync = async (filename, binary = true) => {
  // See the comment in the `readBinary` function.
  filename = isFileURI(filename) ? new URL(filename) : filename;
  var ret = fs.readFileSync(filename, binary ? undefined : 'utf8');
  assert(binary ? Buffer.isBuffer(ret) : typeof ret == 'string');
  return ret;
};
// end include: node_shell_read.js
  if (process.argv.length > 1) {
    thisProgram = process.argv[1].replace(/\\/g, '/');
  }

  programArgs = process.argv.slice(2);

  // MODULARIZE will export the module in the proper place outside, we don't need to export here
  if (typeof module != 'undefined') {
    module['exports'] = Module;
  }

  quit_ = (status, toThrow) => {
    process.exitCode = status;
    throw toThrow;
  };

} else
if (ENVIRONMENT_IS_SHELL) {

} else

// Note that this includes Node.js workers when relevant (pthreads is enabled).
// Node.js workers are detected as a combination of ENVIRONMENT_IS_WORKER and
// ENVIRONMENT_IS_NODE.
if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
  try {
    scriptDirectory = new URL('.', _scriptName).href; // includes trailing slash
  } catch {
    // Must be a `blob:` or `data:` URL (e.g. `blob:http://site.com/etc/etc`), we cannot
    // infer anything from them.
  }

  if (!(globalThis.window || globalThis.WorkerGlobalScope)) throw new Error('not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)');

  {
// include: web_or_worker_shell_read.js
if (ENVIRONMENT_IS_WORKER) {
    readBinary = (url) => {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', url, false);
      xhr.responseType = 'arraybuffer';
      xhr.send(null);
      return new Uint8Array(/** @type{!ArrayBuffer} */(xhr.response));
    };
  }

  readAsync = async (url) => {
    // Fetch has some additional restrictions over XHR, like it can't be used on a file:// url.
    // See https://github.com/github/fetch/pull/92#issuecomment-140665932
    // Cordova or Electron apps are typically loaded from a file:// url.
    // So use XHR on webview if URL is a file URL.
    if (isFileURI(url)) {
      return new Promise((resolve, reject) => {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.responseType = 'arraybuffer';
        xhr.onload = () => {
          if (xhr.status == 200 || (xhr.status == 0 && xhr.response)) { // file URLs can return 0
            resolve(xhr.response);
            return;
          }
          reject(xhr.status);
        };
        xhr.onerror = reject;
        xhr.send(null);
      });
    }
    var response = await fetch(url, { credentials: 'same-origin' });
    if (response.ok) {
      return response.arrayBuffer();
    }
    throw new Error(response.status + ' : ' + response.url);
  };
// end include: web_or_worker_shell_read.js
  }
} else
{
  throw new Error('environment detection error');
}

var out = console.log.bind(console);
var err = console.error.bind(console);

var IDBFS = 'IDBFS is no longer included by default; build with -lidbfs.js';
var PROXYFS = 'PROXYFS is no longer included by default; build with -lproxyfs.js';
var WORKERFS = 'WORKERFS is no longer included by default; build with -lworkerfs.js';
var FETCHFS = 'FETCHFS is no longer included by default; build with -lfetchfs.js';
var ICASEFS = 'ICASEFS is no longer included by default; build with -licasefs.js';
var JSFILEFS = 'JSFILEFS is no longer included by default; build with -ljsfilefs.js';
var OPFS = 'OPFS is no longer included by default; build with -lopfs.js';

var NODEFS = 'NODEFS is no longer included by default; build with -lnodefs.js';

// perform assertions in shell.js after we set up out() and err(), as otherwise
// if an assertion fails it cannot print the message

assert(!ENVIRONMENT_IS_SHELL, 'shell environment detected but not enabled at build time (add `shell` to `-sENVIRONMENT` to enable)');

// end include: shell.js

// include: preamble.js
// === Preamble library stuff ===

// Documentation for the public APIs defined in this file must be updated in:
//    site/source/docs/api_reference/preamble.js.rst
// A prebuilt local version of the documentation is available at:
//    site/build/text/docs/api_reference/preamble.js.txt
// You can also build docs locally as HTML or other formats in site/
// An online HTML version (which may be of a different version of Emscripten)
//    is up at http://kripken.github.io/emscripten-site/docs/api_reference/preamble.js.html

var wasmBinary;

if (!globalThis.WebAssembly) {
  err('no native wasm support detected');
}

// Wasm globals

//========================================
// Runtime essentials
//========================================

// whether we are quitting the application. no code should run after this.
// set in exit() and abort()
var ABORT = false;

// set by exit() and abort().  Passed to 'onExit' handler.
// NOTE: This is also used as the process return code in shell environments
// but only when noExitRuntime is false.
var EXITSTATUS;

// In STRICT mode, we only define assert() when ASSERTIONS is set.  i.e. we
// don't define it at all in release modes.  This matches the behaviour of
// MINIMAL_RUNTIME.
// TODO(sbc): Make this the default even without STRICT enabled.
/** @type {function(*, string=)} */
function assert(condition, text) {
  if (!condition) {
    abort('Assertion failed' + (text ? ': ' + text : ''));
  }
}

// We used to include malloc/free by default in the past. Show a helpful error in
// builds with assertions.
function _malloc() {
  abort('malloc() called but not included in the build - add `_malloc` to EXPORTED_FUNCTIONS');
}
function _free() {
  // Show a helpful error since we used to include free by default in the past.
  abort('free() called but not included in the build - add `_free` to EXPORTED_FUNCTIONS');
}

/**
 * Indicates whether filename is delivered via file protocol (as opposed to http/https)
 * @noinline
 */
var isFileURI = (filename) => filename.startsWith('file://');

// include: runtime_common.js
// include: runtime_exceptions.js
// Base Emscripten EH error class
class EmscriptenEH {}

class EmscriptenSjLj extends EmscriptenEH {}

// end include: runtime_exceptions.js
// include: runtime_debug.js
var runtimeDebug = true; // Switch to false at runtime to disable logging at the right times

// Used by XXXXX_DEBUG settings to output debug messages.
function dbg(...args) {
  if (!runtimeDebug && typeof runtimeDebug != 'undefined') return;
  // TODO(sbc): Make this configurable somehow.  Its not always convenient for
  // logging to show up as warnings.
  console.warn(...args);
}

// Endianness check
(() => {
  var h16 = new Int16Array(1);
  var h8 = new Int8Array(h16.buffer);
  h16[0] = 0x6373;
  if (h8[0] !== 0x73 || h8[1] !== 0x63) abort('Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)');
})();

function consumedModuleProp(prop) {
  var value = Module[prop];
  var msg = `Attempt to modify \`Module.${prop}\` after it has already been processed.  This can happen, for example, when code is injected via '--post-js' rather than '--pre-js'`;
  if (Array.isArray(value)) {
    value = new Proxy(value, {
      set(target, key, val) {
        abort(msg);
        return false;
      },
      defineProperty(target, key, descriptor) {
        abort(msg);
        return false;
      },
      deleteProperty(target, key) {
        abort(msg);
        return false;
      }
    });
  }
  Object.defineProperty(Module, prop, {
    configurable: true,
    get() { return value; },
    set() {
      abort(msg);
    }
  });
}

function makeInvalidEarlyAccess(name) {
  return () => assert(false, `call to '${name}' via reference taken before Wasm module initialization`);

}

function ignoredModuleProp(prop) {
  if (Object.getOwnPropertyDescriptor(Module, prop)) {
    abort(`\`Module.${prop}\` was supplied but \`${prop}\` not included in INCOMING_MODULE_JS_API`);
  }
}

// forcing the filesystem exports a few things by default
function isExportedByForceFilesystem(name) {
  return name === 'FS_createPath' ||
         name === 'FS_createDataFile' ||
         name === 'FS_createPreloadedFile' ||
         name === 'FS_preloadFile' ||
         name === 'FS_unlink' ||
         name === 'addRunDependency' ||
         // The old FS has some functionality that WasmFS lacks.
         name === 'FS_createLazyFile' ||
         name === 'FS_createDevice' ||
         name === 'removeRunDependency';
}

/**
 * Intercept access to a symbols in the global symbol.  This enables us to give
 * informative warnings/errors when folks attempt to use symbols they did not
 * include in their build, or no symbols that no longer exist.
 *
 * We don't define this in MODULARIZE mode since in that mode emscripten symbols
 * are never placed in the global scope.
 */
function hookGlobalSymbolAccess(sym, func) {
  if (!Object.getOwnPropertyDescriptor(globalThis, sym)) {
    Object.defineProperty(globalThis, sym, {
      configurable: true,
      get() {
        func();
        return undefined;
      }
    });
  }
}

function missingGlobal(sym, msg) {
  hookGlobalSymbolAccess(sym, () => {
    warnOnce(`\`${sym}\` is no longer defined by emscripten. ${msg}`);
  });
}

missingGlobal('buffer', 'Please use HEAP8.buffer or wasmMemory.buffer');
missingGlobal('asm', 'Please use wasmExports instead');

function missingLibrarySymbol(sym) {
  hookGlobalSymbolAccess(sym, () => {
    // Can't `abort()` here because it would break code that does runtime
    // checks.  e.g. `if (typeof SDL === 'undefined')`.
    var msg = `\`${sym}\` is a library symbol and not included by default; add it to your library.js __deps or to DEFAULT_LIBRARY_FUNCS_TO_INCLUDE on the command line`;
    // DEFAULT_LIBRARY_FUNCS_TO_INCLUDE requires the name as it appears in
    // library.js, which means $name for a JS name with no prefix, or name
    // for a JS name like _name.
    var librarySymbol = sym;
    if (!librarySymbol.startsWith('_')) {
      librarySymbol = '$' + sym;
    }
    msg += ` (e.g. -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE='${librarySymbol}')`;
    if (isExportedByForceFilesystem(sym)) {
      msg += '. Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you';
    }
    warnOnce(msg);
  });

  // Any symbol that is not included from the JS library is also (by definition)
  // not exported on the Module object.
  unexportedRuntimeSymbol(sym);
}

function unexportedRuntimeSymbol(sym) {
  if (!Object.getOwnPropertyDescriptor(Module, sym)) {
    Object.defineProperty(Module, sym, {
      configurable: true,
      get() {
        var msg = `'${sym}' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the Emscripten FAQ)`;
        if (isExportedByForceFilesystem(sym)) {
          msg += '. Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you';
        }
        abort(msg);
      },
    });
  }
}

var MAX_UINT8  = (2 **  8) - 1;
var MAX_UINT16 = (2 ** 16) - 1;
var MAX_UINT32 = (2 ** 32) - 1;
var MAX_UINT53 = (2 ** 53) - 1;
var MAX_UINT64 = (2 ** 64) - 1;

var MIN_INT8  = - (2 ** ( 8 - 1));
var MIN_INT16 = - (2 ** (16 - 1));
var MIN_INT32 = - (2 ** (32 - 1));
var MIN_INT53 = - (2 ** (53 - 1));
var MIN_INT64 = - (2 ** (64 - 1));

function checkInt(value, bits, min, max) {
  assert(Number.isInteger(Number(value)), `attempt to write non-integer (${value}) into integer heap`);
  assert(value <= max, `value (${value}) too large to write as ${bits}-bit value`);
  assert(value >= min, `value (${value}) too small to write as ${bits}-bit value`);
}

var checkInt1 = (value) => checkInt(value, 1, 1);
var checkInt8 = (value) => checkInt(value, 8, MIN_INT8, MAX_UINT8);
var checkInt16 = (value) => checkInt(value, 16, MIN_INT16, MAX_UINT16);
var checkInt32 = (value) => checkInt(value, 32, MIN_INT32, MAX_UINT32);
var checkInt53 = (value) => checkInt(value, 53, MIN_INT53, MAX_UINT53);
var checkInt64 = (value) => checkInt(value, 64, MIN_INT64, MAX_UINT64);

// end include: runtime_debug.js
// include: runtime_stack_check.js
const stackCookie1 = 0x02135467;
const stackCookie2 = 0x89BACDFE;

// Initializes the stack cookie. Called at the startup of main and at the startup of each thread in pthreads mode.
function writeStackCookie() {
  var max = _emscripten_stack_get_end();
  assert((max & 3) == 0);
  // If the stack ends at address zero we write our cookies 4 bytes into the
  // stack.  This prevents interference with SAFE_HEAP and ASAN which also
  // monitor writes to address zero.
  if (max == 0) {
    max += 4;
  }
  // The stack grow downwards towards _emscripten_stack_get_end.
  // We write cookies to the final two words in the stack and detect if they are
  // ever overwritten.
  HEAPU32[((max)>>2)] = stackCookie1;checkInt32(stackCookie1);
  HEAPU32[(((max)+(4))>>2)] = stackCookie2;checkInt32(stackCookie2);
  // Also test the global address 0 for integrity.
  HEAPU32[((0)>>2)] = 1668509029;checkInt32(1668509029);
}

function u32ToHexString(num) {
  return '0x' + (num >>> 0).toString(16).padStart(8, '0');
}

function checkStackCookie() {
  if (ABORT) return;
  var max = _emscripten_stack_get_end();
  // See writeStackCookie().
  if (max == 0) {
    max += 4;
  }
  var val1 = HEAPU32[((max)>>2)];
  var val2 = HEAPU32[(((max)+(4))>>2)];
  if (val1 != stackCookie1 || val2 != stackCookie2) {
    abort(`Stack overflow! Stack cookie has been overwritten at ${ptrToString(max)}, expected hex dwords ${u32ToHexString(stackCookie2)} and ${u32ToHexString(stackCookie1)}, but received ${u32ToHexString(val2)} ${u32ToHexString(val1)}`);
  }
  // Also test the global address 0 for integrity.
  if (HEAPU32[((0)>>2)] != 0x63736d65 /* 'emsc' */) {
    abort('Runtime error: The application has corrupted its heap memory area (address zero)!');
  }
}
// end include: runtime_stack_check.js
// include: binaryDecode.js
// Prevent Closure from minifying the binaryDecode() function, or otherwise
// Closure may analyze through the WASM_BINARY_DATA placeholder string into this
// function, leading into incorrect results.
/** @noinline */
function binaryDecode(bin) {
  for (var i = 0, l = bin.length, o = new Uint8Array(l), c; i < l; ++i) {
    c = bin.charCodeAt(i);
    o[i] = ~c >> 8 & c; // Recover the null byte in a manner that is compatible with https://crbug.com/453961758
  }
  return o;
}
// end include: binaryDecode.js
// Memory management

var runtimeInitialized = false;



function updateMemoryViews() {
  // When memory growth is disabled this function should be called exactly once.
  assert(!HEAP8, 'updateMemoryViews should only be called once when ALLOW_MEMORY_GROWTH=0');
  var b = wasmMemory.buffer;
  HEAP8 = new Int8Array(b);
  HEAP16 = new Int16Array(b);
  HEAPU8 = new Uint8Array(b);
  
  HEAP32 = new Int32Array(b);
  HEAPU32 = new Uint32Array(b);
  HEAPF32 = new Float32Array(b);
  HEAPF64 = new Float64Array(b);
  HEAP64 = new BigInt64Array(b);
  
}

// include: memoryprofiler.js
// end include: memoryprofiler.js
// end include: runtime_common.js
assert(globalThis.Int32Array && globalThis.Float64Array && Int32Array.prototype.subarray && Int32Array.prototype.set,
       'JS engine does not provide full typed array support');

function preRun() {
  var preRun = Module['preRun'];
  if (preRun) {
    if (typeof preRun == 'function') preRun = [preRun];
    onPreRuns.push(...preRun);
  }
  consumedModuleProp('preRun');
  // Begin ATPRERUNS hooks
  callRuntimeCallbacks(onPreRuns);
  // End ATPRERUNS hooks
}

function initRuntime() {
  assert(!runtimeInitialized);
  runtimeInitialized = true;

  setStackLimits();

  checkStackCookie();

  // No ATINITS hooks

  wasmExports['__wasm_call_ctors']();

  // No ATPOSTCTORS hooks

  checkStackCookie();
}

function postRun() {
  checkStackCookie();

  var postRun = Module['postRun'];
  if (postRun) {
    if (typeof postRun == 'function') postRun = [postRun];
    onPostRuns.push(...postRun);
  }
  consumedModuleProp('postRun');

  // Begin ATPOSTRUNS hooks
  callRuntimeCallbacks(onPostRuns);
  // End ATPOSTRUNS hooks
}

/**
 * @param {string|number=} what
 */
function abort(what) {
  Module['onAbort']?.(what);

  what = `Aborted(${what})`;
  // TODO(sbc): Should we remove printing and leave it up to whoever
  // catches the exception?
  err(what);

  ABORT = true;

  // Use a wasm runtime error, because a JS error might be seen as a foreign
  // exception, which means we'd run destructors on it. We need the error to
  // simply make the program stop.
  // FIXME This approach does not work in Wasm EH because it currently does not assume
  // all RuntimeErrors are from traps; it decides whether a RuntimeError is from
  // a trap or not based on a hidden field within the object. So at the moment
  // we don't have a way of throwing a wasm trap from JS. TODO Make a JS API that
  // allows this in the wasm spec.

  // Suppress closure compiler warning here. Closure compiler's builtin extern
  // definition for WebAssembly.RuntimeError claims it takes no arguments even
  // though it can.
  // TODO(https://github.com/google/closure-compiler/pull/3913): Remove if/when upstream closure gets fixed.
  /** @suppress {checkTypes} */
  var e = new WebAssembly.RuntimeError(what);

  // Throw the error whether or not MODULARIZE is set because abort is used
  // in code paths apart from instantiation where an exception is expected
  // to be thrown when abort is called.
  throw e;
}

// show errors on likely calls to FS when it was not included
function fsMissing() {
  abort('Filesystem support (FS) was not included. The problem is that you are using files from JS, but files were not used from C/C++, so filesystem support was not auto-included. You can force-include filesystem support with -sFORCE_FILESYSTEM');
}
var FS = {
  init: fsMissing,
  createDataFile: fsMissing,
  createPreloadedFile: fsMissing,
  createLazyFile: fsMissing,
  open: fsMissing,
  mkdev: fsMissing,
  registerDevice:  fsMissing,
  analyzePath: fsMissing,
  ErrnoError: fsMissing,
};


function createExportWrapper(name, func, nargs) {
  assert(func);
  return (...args) => {
    assert(runtimeInitialized, `native function \`${name}\` called before runtime initialization`);
    // Only assert for too many arguments. Too few can be valid since the missing arguments will be zero filled.
    assert(args.length <= nargs, `native function \`${name}\` called with ${args.length} args but expects ${nargs}`);
    return func(...args);
  };
}

var wasmBinaryFile;

function findWasmBinary() {
  return binaryDecode(' asm   ` `  `||` |` ``|||`||||`| `||`|| `|||`|~```|||| `}}`|`|`}}} ``|`||``~~` env__handle_stack_overflow HG	\n \n 	            \r  \rpAA A A A þmemory __wasm_call_ctors init update_game render_game get_framebuffer \nget_thirst get_x get_y 	get_angle 	get_score get_time  	get_sound !\nget_sprint "get_third_person #toggle_third_person $	get_combo %\rget_sandstorm &	set_input \'main *__indirect_function_table fflush Femscripten_stack_init ;emscripten_stack_get_free <emscripten_stack_get_base =emscripten_stack_get_end >_emscripten_stack_restore ?_emscripten_stack_alloc @emscripten_stack_get_current A__set_stack_limits G\nñG ; Õ-tH|# A k!   "r#K r#Ir@ r  r$   A 6@@  (AÀ HAqE\r  A 6@@  (AÀ HAqE\r    (  (:     (AlAj  (AlA\rj:     (AlAj  (AlAá j:     (Ao6    (A o6  (!A!@ E\r   (AF!A ! Aq! !@ E\r   (AN! !   Aq:   (!A!@ E\r   (AF!	A !\n 	Aq! \n!@ E\r Aÿ  - AÈJ! !   Aq: A !\rAÿ  -  \rAÿqG!A! Aq! !@ \r A !Aÿ  -  AÿqG!   Aq:   A ·9  A ·9øA !Aÿ@  -  AÿqGAq\r @@  (\r A!  (!   6ô@@  (ôAHAqE\r   (ô·!tD       @ t¡D       @£!u  (ô·D       @¡D       @£!u   u9@@  (AHAqE\r   (·!vD       @ v¡D       @£!w  (·D       @¡D       @£!w   w9ø    +DÙ?¢D      ð? 9    +øD333333Ó?¢D      ð? 9ø    +  +ø¢9èA !Aÿ@@  -  AÿqGAqE\r Aÿ    - AmA¥j: çAÿ    - AmAj: æAÿ    - AmAø j: å    - AvAÈj·  +è¢ü: ç    - A\nnA´j·  +è¢ü: æ    - AnAj·  +è¢ü: åAÿ@  - AæJAqE\r Aÿ    - çAk: çAÿ    - æA\nk: æAÿ    - åAk: åAÿ@  - AõJAqE\r Aÿ    - çAj: çAÿ    - æAj: æAÿ    - åAj: å  - ç¸!xD     ào@!yD        !z x z yü!  - æ¸ z yü!  - å¸ z yü!Aÿ! Aÿq Aÿq Aÿq Aÿq!  (At  (j!A Ü  Atj 6     (A o6à    (A o6Ü  (à!A!@ E\r   (Ü!A! E\r   (àAF!A!  Aq!!  ! !\r   (ÜAF!   Aq: Û    (à·D      0@¡D      0@£9Ð    (Ü·D      0@¡D      0@£9È  +Ð!{  +Ð!|    +È  +È¢ { |¢ D333333Ã?¢D      ð? 9ÀA !"Aÿ@@  - Û "AÿqGAqE\r Aÿ    - A\nmAÚ j: ¿Aÿ    - AmAÒ j: ¾Aÿ    - AmAÆ j: ½    +À9°  - AnA j·  +°¢!}D     ào@!~D        !   }  ~ü: ¿    - AvAj·  +°¢  ~ü: ¾    - A\nnAj·  +°¢  ~ü: ½Aÿ@  - AðJAqE\r Aÿ    - ¿Ak: ¿Aÿ    - ¾Ak: ¾Aÿ    - ½Ak: ½  - ¿!#  - ¾!$Aÿ!% #Aÿq!& $Aÿq!\'Aÿ & \'  - ½ %Aÿq!(  (At  (j!)A Ü Aj )Atj (6   (·!Aÿ    - ·Dú~j¼t?¢ D      à?¢ 8D333333Ã?¢D333333ë? 9¨  (·!    (·Dffffffæ?¢ D333333Ó?¢ 8D{®Gáz´?¢Dq=\n×£pí? 9 @@  (AHAqE\r   (!*A *k·D       @£!@@  (A>NAqE\r   (A>k·D       @£!A ·! !   9@@  (AHAqE\r   (!+A +k·D       @£!@@  (A>NAqE\r   (A>k·D       @£!A ·! !   9    +  + DÙ¿¢D      ð? 9    +¨  + ¢  +¢9  +!  - An· D     à`@¢ !D     ào@!D        !     ü:   +!    - An· D     ÀW@¢   ü: ~  +!    - An· D     F@¢   ü: }@  (Ao\r Aÿ    - Ak: Aÿ    - ~Ak: ~Aÿ    - }A\nk: }  - !,  - ~!-Aÿ!. ,Aÿq!/ -Aÿq!0Aÿ / 0  - } .Aÿq!1  (At  (j!2A Ü Aj 2Atj 16   A ·9pAÿ@  - AáJAqE\r     (Ao6l    (Ao6h  (lAk·!  (lAk·!    (hAk·  (hAk·¢  ¢ 9`@  +`D      @cAqE\r   +`D      @£!  D      ð? ¡9p  (·!    (·D¹?¢ D333333Ó?¢ 8D{®Gáz¤?¢9X    +XD      ð? 9P  - AnA»j·!  +P!  +pD      9À¢  ¢ !D     ào@!D        !   ü!3  - AvAj·!  +P!  +pD      2À¢  ¢   ü!4  - A\nnAé j·!  +P!  +pD      $À¢  ¢   ü!5Aÿ!6 3Aÿq 4Aÿq 5Aÿq 6Aÿq!7  (At  (j!8A Ü Aj 8Atj 76     (A oAHAq: O    (A oAHAq: NA !9Aÿ  - O 9AÿqG!:A!; :Aq!< ;!=@ <\r A !>Aÿ  - N >AÿqG!=   =Aq: M  (·!  (·DÉ?¢ D333333Ó?¢ !Aÿ     - ·D{®Gáz?¢ 8D¸ëQ¸®?¢D®Gázî? 9@A !?Aÿ@@  - M ?AÿqGAqE\r   (·!    - ·D¸ëQ¸?¢ Dé?¢ 8D¹?¢DÍÌÌÌÌÌì? 90  +0!  - An· D     ÀW@¢ !D     ào@!D        !     ü: ?  +0!     - Av·  D      R@¢   ü: >  +0!¡    - An· ¡D      D@¢   ü: =  - A\nnAj·  +@¢!¢D     ào@!£D        !¤   ¢ ¤ £ü: ?    - AnAj·  +@¢ ¤ £ü: >    - AnAé j·  +@¢ ¤ £ü: =Aÿ@  - AëJAqE\r Aÿ    - ?A\nk: ?Aÿ    - >Ak: >Aÿ    - =Ak: =  - ?!@  - >!AAÿ!B @Aÿq!C AAÿq!DAÿ C D  - = BAÿq!E  (At  (j!FA Ü Aj FAtj E6     (Ao6,  (,!GA!H@ GE\r   (,AF!H   HAq: +  (,AF!IA!J IAq!K J!L@ K\r   (,AF!L   LAq: *@@  (ANAqE\r   (AL!MA!N MAq!O N!P O\r  (A,N!QA !R QAq!S R!T@ SE\r   (A0L!T T!P   PAq: )  A : (    (A o6$    (A o6 @@  ($AFAq\r   ($AFAqE\r@  ( AFAq\r   ( AFAq\r   ( A$FAq\r   ( A,FAqE\r  ($!U  ($AH!V UAA VAqk·!¥  ($!W  ($AH!X WAA XAqk·!¦  ( !Y@@  ( AHAqE\r   ( AH!ZAA ZAq![  ( A(H!\\A$A, \\Aq![ Y [k·!§  ( !]@@  ( AHAqE\r   ( AH!^AA ^Aq!_  ( A(H!`A$A, `Aq!_   § ] _k·¢ ¥ ¦¢ 9@  +D      @cAqE\r   A: (  (·!¨Aÿ    - ·D{®Gáz?¢ ¨D333333ã?¢ 8D{®Gáz´?¢Dq=\n×£pí? 9A !aAÿ@@  - ( aAÿqGAqE\r   ($A oAk·!©  ($A oAk·!ª  ( A o!b  ( AH!c bAA$ cAqk·!«  ( A o!d  ( AH!e « dAA$ eAqk·¢ © ª¢ D      @£!¬  D      ð? ¬¡9 @  + A ·cAqE\r   A ·9   + D      D@¢D      N@   - Av· !­D     ào@!®D        !¯   ­ ¯ ®ü:     + D     A@¢D     K@   - An·  ¯ ®ü:     + D      >@¢D      I@   - An·  ¯ ®ü: \rA !fAÿ@@  - ) fAÿqGAqE\r   +!°  - An· °D     ÀR@¢ !±D     ào@!²D        !³   ± ³ ²ü:   +!´    - Av· ´D     K@¢  ³ ²ü:   +!µ    - An· µD      >@¢  ³ ²ü: \rA !gAÿ@@@  - + gAÿqGAq\r A !hAÿ  - * hAÿqGAqE\r  - AvA7j·!¶D     ào@!·D        !¸   ¶ ¸ ·ü:     - AnA*j· ¸ ·ü:   - !iA!j   j i jnj· ¸ ·ü: \r  - AvAø j·  +¢!¹D     ào@!ºD        !»   ¹ » ºü:     - A\nnAØ j·  +¢ » ºü:     - AnA-j·  +¢ » ºü: \r  - !k  - !lAÿ!m kAÿq!n lAÿq!oAÿ n o  - \r mAÿq!p  (At  (j!qA Ü Aj qAtj p6     (Aj6      (Aj6    A j"s#K s#Ir@ s  s$ µN|}~# A°k!   "L#K L#Ir@ L  L$ A â !A2! A  ü A !A  6 ¼bA ·!NA  N9¨¼bA ·!OA  O9°¼bA ·!PA  P9¸¼bA !A  6À¼bA*5  A#6¬  A#6¨  (¬!A  6Ä¼b  (¨!A  6È¼b  (¬A\nj!A  6Ì¼b  (¨A\nj!A  6Ð¼b  A 6¤@@  (¤AHAqE\r  (¬  (¤j!	A â  	AÐ lj  (¨jA:    (¬  (¤j!\nA â  \nAÐ lj  (¨A\njjA:    (¬!A â  AÐ lj  (¨  (¤jjA:    (¬A\nj!A â  AÐ lj  (¨  (¤jjA:      (¤Aj6¤    (¬Aj!\rA â  \rAÐ lj  (¨A\njjA:    (¬Aj!A â  AÐ lj  (¨A\njjA:    (¬Aj!A â  AÐ lj  (¨jA:    (¬Aj!A â  AÐ lj  (¨jA:    (¬A\nj!A â  AÐ lj  (¨AjjA:    A6 @@  ( A\nHAqE\r  A6@@  (A\nHAqE\r  (¬  (j!A â  AÐ lj  (¨  ( jjA:      (Aj6      ( Aj6     (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:    (¬Aj!A â  AÐ lj  (¨AjjA:  A!A !  Að j  ü\n    A 6l@@  (lAHAqE\r  (l!    Að j Atj( 6h  (l!    Að j Atj(6d  (hA(k!   Au!!@@   !s !kAHAqE\r   (dA(k!" "Au!# " #s #kAHAqE\r   A 6`@@  (`AHAqE\r  A 6\\@@  (\\AHAqE\r    (h  (`j6X    (d  (\\j6T@  (XA NAqE\r   (XAÐ HAqE\r   (TA NAqE\r   (TAÐ HAqE\r   (X!$A â  $AÐ lj  (TjA:      (\\Aj6\\      (`Aj6`  @A ( ¼bAHAqE\r   (h·D      ð? !QA ( ¼b!%Aà¼â  %Atj Q9   (d·D      ð? !RA ( ¼b!&Aà¼â  &Atj R9A ( ¼bAj!\'A  \'6 ¼b    (lAj6l    (¬·D      @ !SA  S9Ð\\  (¨·D       @ !TA  T9Ø\\D-DTû!ù¿!UA  U9à\\D      Y@!VA  V9ÀA !(A  (6è\\A ·!WA  W9ð\\A ·!XA  X9ÀÀbA !)A  )6\\A ·!YA  Y9ÈÀbA !*A  *6ÐÀbD      9@!ZA  Z9ÈA !+A  +6\\A ·![A  [9ØÀbA !,A  ,6àÀb  A 6P@  (PAø H!-A !. -Aq!/ .!0@ /E\r A (àÀbAø H!0@ 0AqE\r 6Aèo·D     @@£D      @¢¶!^A (àÀbAtAðÀâ j ^8 6AÈo·D      i@£D     f@¢¶!_A (àÀb!1AðÀâ  1Atj _86A¼o²C  zDC>!`A (àÀb!2AðÀâ  2Atj `86Aôo²C  ÈB!aA (àÀb!3AðÀâ  3Atj a8A (àÀbAj!4A  46àÀb    (PAj6PA !5A  56ðÏb  A 6L@  (LAH!6A !7 6Aq!8 7!9@ 8E\r A (ðÏbA(H!9@ 9AqE\r   6A<o·D      $@ 9@  6A<o·D      $@ 98  +@üA(k!: :Au!;@@ : ;s ;kAHAqE\r   +8üA(k!< <Au!= < =s =kAHAqE\r   +@ü!>A â  >AÐ lj  +8üj!?Aÿ@ ?-  E\r   6Ao: 7  +@!\\A (ðÏb!@AÐâ  @Alj \\9   +8!]A (ðÏb!AAÐâ  AAlj ]9  - 7!BA (ðÏb!CAÐâ  CAlj B: A (ðÏb!DAÐâ  DAljA6A (ðÏbAj!EA  E6ðÏb    (LAj6LAÀ×â !FA2!G FA  Gü     (¨Ak60@@  (0  (¨AjLAqE\r    (¬Ak6,@@  (,  (¬AjLAqE\r@  (,A NAqE\r   (,AÐ HAqE\r   (0A NAqE\r   (0AÐ HAqE\r   (,!HAÀ×â  HAÐ lj  (0jA:      (,Aj6,      (0Aj60    A 6(@@  ((AÈHAqE\rB !b   b7    b7   b7   b7  ((!IAÀã  IAtj!J J  ) 7 J  )7 J  )7 J  )7   ((!KAÀã  KAtjA ²8    ((Aj6(    A°j"M#K M#Ir@ M  M$ h# Ak!   6  6  (A±ÏÙ²l (A¯ÖÓ¾lj6  ( (A\rusAá¾Æßl6 ( (AusAÿqAÿqp|# A k!   9  9  9@@ + +cAqE\r  +!@@ + +dAqE\r  +! +! ! d# Ak!   :   :   : \r  : Aÿ - At!Aÿ  - \rAtr!Aÿ  - Atr!Aÿ  - rM# Ak! "#K #Ir@   $    9 +	 Aj"#K #Ir@   $ (PB|}~# Ak! "O#K O#Ir@ O  O$    9ø +ø!QA !  Q +ð\\ 9ð\\  6ø\\ (ü\\!Dffffff@!R D      @ R  +ø¢9ð  +øDffffff@¢9è *\\»!S +è!T  +à\\ S T¢ 9à\\ +à\\!U U/ *\\»¢!V +ð!WD-DTû!ù?!X  W U X / *\\»¢¢ V W¢ 9à +à\\!Y Y8 *\\»¢!Z +ð![  Y X 8 *\\»¢ +ð¢ Z [¢ 9Ø D      Ð?9Ð A +Ð\\ +à 9È A +Ø\\ +Ø 9À@ +È +Ð A +Ø\\\nAqE\r  +È +Ð¡A +Ø\\\nAqE\r  +ÈA +Ø\\ +Ð \nAqE\r  +ÈA +Ø\\ +Ð¡\nAqE\r  +È!\\A  \\9Ð\\@A +Ð\\ +Ð  +À\nAqE\r A +Ð\\ +Ð¡ +À\nAqE\r A +Ð\\ +À +Ð \nAqE\r A +Ð\\ +À +Ð¡\nAqE\r  +À!]A  ]9Ø\\ +à!^ +à!_  +Ø +Ø¢ ^ _¢  +ø£9¸@ +¸D      à?dAqE\r  +ø +¸¢!`A +ÀÀb `D333333ã?¢ !aA  a9ÀÀb@ +¸D      à?dAqE\r A +ð\\A +À»c¡DffffffÖ?dAqE\r A (ø\\Ar!A  6ø\\A +ð\\!bA  b9À»c@ +¸D      ð?dAqE\r 6Ao\r A +Ð\\A +Ø\\ +à +¸£ +Ø +¸£A ! *\\! *\\!    »D333333ë?d! AA  Aq6´@ (´E\r A (À¼b\r D      @!cA  c9¸¼b (´!A  6À¼b (´!A  6ü\\ A#6° A#6¬  (°·D      @ 9   (¬·D      %@ 9A +Ð\\ + ¡!dA +Ð\\ + ¡!e A +Ø\\ +¡A +Ø\\ +¡¢ d e¢ 9 +D      @c!	D      ð?A · 	Aq!fA  f9°¼b A +¨¼b9  +øD      @¢9@@A +¨¼bA +°¼bcAqE\r  +A +¨¼b !gA  g9¨¼b@A +¨¼bA +°¼bdAqE\r A +°¼b!hA  h9¨¼b +!iA +¨¼b i¡!jA  j9¨¼b@A +¨¼bA +°¼bcAqE\r A +°¼b!kA  k9¨¼b@ +D¹?cAqE\r A +¨¼bD¹?fAqE\r A (ø\\Ar!\nA  \n6ø\\@ +D¹?dAqE\r A +¨¼bD©?eAqE\r A (ø\\Ar!A  6ø\\A +Ð\\ü! A â  AÐ ljA +Ø\\üj-  : Aÿ@ - AFAqE\r A +ÀD      Y@cAqE\r D      Y@!lA  l9ÀA (è\\Aj!\rA  \r6è\\A (ø\\Ar!A  6ø\\A +Ð\\A +Ø\\\r +ø!mA +À mD      À¢ !nA  n9À@A +ÀA ·cAqE\r A ·!oA  o9À@A +ÀA ·dAqE\r A +ÀD      4@cAqE\r D      ø?A +ð\\D      ø?3 +øcAqE\r A (ø\\Ar!A  6ø\\@A +ÀA ·dAqE\r A +ÀD      4@cAqE\r Dé?A +ð\\Dé?3 +øcAqE\r A (ø\\Ar!A  6ø\\ A 6x@@ (xA (ðÏbHAqE\r (x!@@AÐâ  Alj(\r A +Ð\\!p (x!  pAÐâ  Alj+ ¡9pA +Ø\\!q (x!  qAÐâ  Alj+¡9h +p!r +p!s@ +h +h¢ r s¢ D      ø?cAqE\r  (x!AÐâ  AljA 6 (x!AÐâ  Alj!Aÿ - AtAjA (è\\j!A  6è\\A (\\Aj!A  6\\D      @!tA  t9ÈÀb@A (\\A (ÐÀbJAqE\r A (\\!A  6ÐÀb (xAlAÐâ j-  ! AK@@@@  A +ÀD      9@ A ·D      Y@!uA  u9ÀA (ø\\Ar!A  6ø\\D       @!vA  v9¸¼bA (ø\\Ar!A  6ø\\A (ø\\Ar!A  6ø\\ A 6d@@ (dAHAqE\r 6`@ (`A HAqE\r  6Aôo·D      Y@£9XB !¢  ¢7P  ¢7H  ¢7@  ¢78 (`!A!  t! AÀã !!   !j!"  AØã j )P7   AÐã j )H7 AÈã !#   #j )@7  " )87  (x!$A!% $ %lAÐâ j+ ¶! ! (` tj 8  % (xlAÐâ j+ ¶! (` tAÄã j 8  +X/!wD      ø?!x w x¢¶! # (` tj 8  x +X8¢¶! (`!&AÀã  &Atj 86Ao²C   A! (`!\'AÀã  \'Atj 8 (`!(AÀã  (Atj*! (`!)AÀã  )Atj 8Aÿ!*AÜ!+A<!, *Aÿq +Aÿq ,Aÿq *Aÿq!- (`!.AÀã  .Atj -6 (`!/AÀã  /AtjA:   (dAj6d    (xAj6x  @A +ÈÀbA ·dAqE\r  +ø!yA +ÈÀb y¡!zA  z9ÈÀb@A +ÈÀbA ·eAqE\r A !0A  06\\ +ø!{A +È {¡!|A  |9È@A +ÈA ·eAqE\r A (\\\r A!1A  16\\A ·!}A  }9ØÀbA (ø\\Ar!2A  26ø\\@A (\\E\r  +ø!~A +ØÀb ~D333333Ã?¢ !A  9ØÀb@A +ØÀbD      ð?dAqE\r D      ð?!A  9ØÀb +øD      ø?¢!A +ØÀb!A +À  ¢ !A  9À@A +ÈD      4ÀcAqE\r A !3A  36\\A ·!A  9ØÀb6Ao·D      >@ !A  9È@A (\\\r A +ÈD      4ÀcAqE\r 6Ao·D      >@ !A  9È A64 A +Ø\\ü (4k60@@ (0A +Ø\\ü (4jLAqE\r A +Ð\\ü (4k6,@@ (,A +Ð\\ü (4jLAqE\r@ (,A NAqE\r  (,AÐ HAqE\r  (0A NAqE\r  (0AÐ HAqE\r  (,!4AÀ×â  4AÐ lj (0jA:    (,Aj6,    (0Aj60  @A +ÀA ·eAqE\r A (ø\\Ar!5A  56ø\\A +Ð\\A +Ø\\D       @!A  9¸¼b (°·D      @ !A  9Ð\\ (¬·D       @ !A  9Ø\\D-DTû!ù¿!A  9à\\D      Y@!A  9À@@A +¸¼bD{®Gáz?dAqE\r 6Aä oA2k·D      $@£A +¸¼b¢D       @£!A  9È»c6Aä oA2k·D      $@£A +¸¼b¢D       @£!A  9Ð»cA +¸¼bD)\\Âõ(ì?¢!A  9¸¼bA ·!A  9È»cA ·!A  9Ð»cA ·!A  9¸¼b A 6(@@ ((AÈHAqE\r ((!6 AÀã  6Atj6$@@ ($*A ²_AqE\r  ($*! ($!7 7  7* 8  ($*! ($!8 8  8*8 ($!9 9 9*C  ?8 ($!:Aÿ@ :- AFAqE\r  ($!; ; ;*CÂõ<8 ($!<Aÿ@ <- AFAqE\r  ($!= = =*CÍÌL=8  ((Aj6(  @A +ð\\A +Ø»c¡D{®Gáz´?dAqE\r A +ð\\!A  9Ø»c 6 @ ( A NAqE\r B !£  £7  £7  £7  £7  ( !>A!? > ?t!@AÀã !A @ Aj!B @AØã j )7  @AÐã j )7 AÈã !C @ Cj )7  B ) 7  A (  ?tjA|6 6AÂo²! (  ?tAÄã j 8 6Ao²C  ÈBC>! C (  ?tj 8 6AoAvj·D      Y@£¶! ( !DAÀã  DAtj 86AÈo²C  HC!  ( !EAÀã  EAtj  8 ( !FAÀã  FAtj*!¡ ( !GAÀã  GAtj ¡86AoAÒj!H6AoAÃj!I6AoAj!JAÿ!K HAÿq IAÿq JAÿq KAÿq!L ( !MAÀã  MAtj L6 ( !NAÀã  NAtjA :  Aj"P#K P#Ir@ P  P$ \n# A k!   9  9@@@ +D      Ð?cAq\r  +D     ðS@fAq\r  +D      Ð?cAq\r  +D     ðS@fAqE\r A Aq:  +ü! A â  AÐ lj +üj-  : Aÿ - !A!@ E\r Aÿ - AF!A! Aq! ! \r Aÿ - AF!	A!\n 	Aq! \n! \r Aÿ - AF!  Aq:  - Aqë~|}# AÐ k! "#K #Ir@   $    9H  9@  98  90 6,@@ (,A HAqE\r B !  7   7  7  7 (,!A!  t!AÀã !  j!	 AØã j ) 7  AÐã j )7 AÈã !\n  \nj )7  	 )7  +H! +8!D333333Ó?!   ¢¡¶!"  (, tj "8  +@  +0¢¡¶!# (, tAÄã j #8 6!A(!  o!\rAl! \r j·! D      Y@!!   !£¶!$ \n (, tj $8  6 oj· !£¶!% (,!AÀã  Atj %86Ao²C  ÈA!& (,!AÀã  Atj &8 (,!AÀã  Atj*!\' (,!AÀã  Atj \'8AÈ!A´!A!Aÿ! Aÿq Aÿq Aÿq Aÿq! (,!AÀã  Atj 6 (,!AÀã  AtjA:  AÐ j"#K #Ir@   $ # Ak!   8 *|~}# AÐ k! "#K #Ir@   $    9H  9@ A 6<@@ (<AHAqE\r 68@ (8A HAqE\r 6Aôo·!D      Y@!   £90 6AÈo· £D      à? 9(B !  7   7  7  7 (8!A!  t!AÀã !  j! AØã j ) 7  AÐã j )7 AÈã !  j )7   )7  +H¶!  (8 tj 8  +@¶! (8 tAÄã j 8  +0/ +(¢¶!  (8 tj 8  +08 +(¢D      ø¿¢¶! (8!	AÀã  	Atj 86Ao²C  ðA! (8!\nAÀã  \nAtj 8 (8!AÀã  Atj*! (8!AÀã  Atj 8AÐ !\rA !Að!Aÿ! \rAÿq Aÿq Aÿq Aÿq! (8!AÀã  Atj 6 (8!AÀã  AtjA:   (<Aj6<   AÐ j"#K #Ir@   $ u# Ak!   A 6@@@  (AÈHAqE\r  (!@AÀã  Atj*A ²_AqE\r     (6    (Aj6    A6  (|~}# AÐ k! "#K #Ir@   $    9H  9@ A 6<@@ (<A(HAqE\r 68@ (8A HAqE\r 6Aôo·!D      Y@!   £90 6A¬o· £D      ð? 9(B !  7   7  7  7 (8!A!  t!AÀã !  j! AØã j ) 7  AÐã j )7 AÈã !  j )7   )7  +H¶!  (8 tj 8  +@¶! (8 tAÄã j 8  +0/ +(¢¶!  (8 tj 8  +08 +(¢¶! (8!	AÀã  	Atj 86A(o²C  pB! (8!\nAÀã  \nAtj 8 (8!AÀã  Atj*! (8!AÀã  Atj 86Aä oAä j!\r6AoAj!Aÿ! Aÿq \rAÿq Aÿq Aÿq! (8!AÀã  Atj 6 (8!AÀã  AtjA:   (<Aj6<   AÐ j"#K #Ir@   $  °²»|}# A°\rk!   "°#K °#Ir@ °  °$ D      ð?  BåæµÁ£ª°ø?7¨\r    +¨\rD      à?¢:9 \rA +ð\\D      ^@£!²   ² ²¡ ²¦9\r    +\rD-DTû!	@¢9\r    +\r8DÙ?¢D333333ã? 9\r    +\rD      @¢D      T@ ü6\r    +\r8D     °cÀ¢D     Pi@ ü6\r  A +ð\\D      à?¢8D      @¢D      (@ 9ø  6ô    +\rDPië1Ç(î?¡/9è  D333333Ó?9à  A +Ð\\A +Ø\\Aq: ß  A +È»cü6Ø  A +Ð»cü6Ô@@A (\\E\r   D      @9È  D      ø?9ÀA +Ð\\!³A +à\\/!´ ³  +È ´¢ !µA  µ9à»cA +Ø\\!¶A +à\\8!· ¶  +È ·¢ !¸A  ¸9è»c@A +à»cA +è»c\nAq\r A +Ð\\!¹A  ¹9à»cA +Ø\\!ºA  º9è»cA +Ð\\!»A  »9à»cA +Ø\\!¼A  ¼9è»c  A 6¼@@  (¼A HAqE\r  A +à\\  +¨\rD       @£¡  (¼·D      @£  +¨\r¢ 9°    +°/9¨    +°89   A +à»cü6  A +è»cü6@@  +¨A ·aAqE\r Dê 9Y>)F!½  +¨!¾D      ð? ¾£!½   ½9@@  + A ·aAqE\r Dê 9Y>)F!¿  + !ÀD      ð? À£!¿   ¿9@@  +¨A ·cAqE\r   A6ô  A +à»c  (·¡  +¢9  A6ô    (·D      ð? A +à»c¡  +¢9@@  + A ·cAqE\r   A6ð  A +è»c  (·¡  +¢9ø  A6ð    (·D      ð? A +è»c¡  +¢9ø  A 6ì  A 6è  A : ç  A 6à@@  (àAHAqE\r@@  +  +øcAqE\r     +  + 9    (ô  (j6  A 6ì    +  +ø 9ø    (ð  (j6  A6ì@@  (A HAq\r   (AÐ NAq\r   (A HAq\r   (AÐ NAqE\r  (!  A â  AÐ lj  (j-  : çAÿ@@  - çAFAq\r Aÿ  - çAFAqE\r  A6èAÿ@  - çAFAqE\r A +¨¼bD333333ë?cAqE\r   A6è    (àAj6à  @@  (èE\r @@  (ì\r   +  +¡!Á  +ø  +¡!Á   Á9Ø@  +ØD{®Gáz?cAqE\r   D{®Gáz?9Ø@@  (ì\r   (·!Â  (ôA H!   ÂD      ð?A · Aq 9Ð  A +è»c  +Ø  + ¢ 9È  A +à»c  +Ø  +¨¢ 9Ð  (·!Ã  (ðA H!   ÃD      ð?A · Aq 9È  D      I@9Ø  A +à»c  +Ø  +¨¢ 9Ð  A +è»c  +Ø  + ¢ 9È  +Ø!Ä  D      |@ Ä£ü6Ä  (Ä!  A  kAmAáj  (ôk6À    (ÄAmAáj  (ôk6¼@@  (ÀA HAqE\r A !  (À!   6¸@@  (¼AÂNAqE\r AÁ!  (¼!   6´@@  (èE\r @@  (ì\r   +È  +È¡!Å  +Ð  +Ð¡!Å Å!ÆA ·!Æ   Æ9¨    +¨D      P@¢üA?q6¤@@@  (ì\r   +¨A ·dAq\r  (ìAFAqE\r  + A ·cAqE\r  (¤!  AÀ  kAk6¤A !Aÿ@@  - ß AÿqGAqE\r   A 6 @@  (   (¸HAqE\r  ( ·!ÇD      l@ Ç¡  (ô· D       @¢!È  D      |@ È£9@  +D¹?cAqE\r   D¹?9A !	   	+à»c  +  +¨¢ 9   	+è»c  +  + ¢ 9  +!ÉD      P@!Ê É Ê¢ü!\nA?!   \n q6    Ê  +¢üq6    (At  (jAtA à j( 6ü\n    (ü\n: û\n    (ü\nAv: ú\n    /þ\n: ù\n  +D      >À£!ËDá?!Ì   Ë Ì DÉ? Ì  +\r¢9ð\n  - û\n·  +ð\n¢!ÍD     ào@!ÎD        !Ï   Í Ï Îü: û\n    - ú\n·  +ð\n¢ Ï Îü: ú\n    - ù\n·  +ð\n¢ Ï Îü: ù\n  - û\n!  - ú\n!\rAÿ! Aÿq! \rAÿq!Aÿ    - ù\n Aÿq  +!  ( A l  (¼j!AÐ Atj 6     ( Aj6     A 6ì\n@@  (ì\n  (¸HAqE\r    (ì\n·D      l@£9à\n  +à\n!Ð Ð Ð¢!Ñ Ð Ð !Ò   ÑD      @ Ò¡¢9Ø\n  +Ø\n!ÓD     k@!ÔD     F@ Ô Ó  +\r¢!ÕD     ào@!ÖD        !×   Õ × Öü: ×\n  +Ø\n!ØD     `h@!Ù  D     Q@ Ù Ø  +\r¢ × Öü: Ö\n  +Ø\n!ÚD      d@!Û  D     a@ Û Ú  +\r¢ × Öü: Õ\n    (¼  (\rk·9È\n    (ì\n  (\rk·9À\n  +È\n!Ü  +È\n!Ý    +À\n  +À\n¢ Ü Ý¢ 9¸\n@  +¸\n  +øD      @¢cAqE\r     +¸\n  +øD      À¢£D      ð? 9°\n  +°\n!Þ   Þ Þ¢9°\n  - ×\n·!ß  +°\n!àD     ào@!á ß à á¢  +\r¢ !âD        !ã   â ã áü: ×\n    - Ö\n·  +°\nD      i@¢  +\r¢  ã áü: Ö\n    - Õ\n·  +°\nD      Y@¢  +\r¢  ã áü: Õ\n@  +¸\n  +øcAqE\r   +¸\n  +ø£!ä  D      ð? ä¡9¨\n  +¨\n!å   å å¢9¨\n  - ×\n¸!æ  +¨\n!çD     ào@!è æ è ç!éD        !ê   é ê èü: ×\n  - Ö\n¸!ë  +¨\n!ì   ëD      n@ ì ê èü: Ö\n  - Õ\n¸!í  +¨\n!î   íD     f@ î ê èü: Õ\n  - ×\n!  - Ö\n!Aÿ! Aÿq! Aÿq!Aÿ    - Õ\n Aÿq!  (ì\nA l  (¼j!AÐ Atj 6     (ì\nAj6ì\n  @  +\rDffffffæ?cAqE\r     +\rDffffffæ¿£D      ð? ¶8¤\n  A 6 \n@@  ( \nA (àÀbHAqE\r  ( \n!  AðÀâ  Atj* ü 6\n  ( \n!  AðÀâ  Atj*C  áCC  4Cü 6\n@@@  (\nA HAq\r   (\n  (¸NAqE\r    ( \nAtAüÀâ j* »A +ð\\D      ø?¢ 8D   @33Ó?¢D   `ffæ? ¶8\n  ( \n!  AðÀâ  Atj*  *\n  *¤\n8\n@  (\n  (¼FAqE\r     *\nC  Cü: \n  (\nA l  (¼j!AÐ Atj( !Aÿ!AÜ!  Aÿq!! Aÿq!"  Aÿq!#Aÿ  ! " #  - \n!$  (\nA l  (¼j!%AÐ %Atj $6     ( \nAj6 \n  @  (èE\r Aÿ@@  - çAFAqE\r A Ü Aj!&Aÿ@@  - çAFAqE\r A Ü Aj!\'A Ü !\' \'!&   &6\n    (¸6\n@@  (\n  (´LAqE\r    (\n  (Àk·  (Ä·£D      P@¢üA?q6\n    (\n  (\nAt  (¤jAtj( 6ü	    (ü	Aÿq: û	    (ü	AvAÿq: ú	    (ü	AvAÿq: ù	  (ì!(  D\n×£p=\nç?D      ð? (9ð	    (\n  (Àk·  (Ä·£9è	  +è	!ï  D      ð? ï¡D      Ð?¢D      è? 9à	  D      ð?9Ø	@  +è	D333333ë?dAqE\r     +è	D333333ë?¡D      @¢D      ð? 9Ø	Aÿ@@  - çAFAqE\r A +¨¼bD©?dAqE\r A +¨¼b!ð  D      ð? ð¡9Ð	@  +Ð	D©?cAqE\r   D©?9Ð	    - û	·  +Ð	¢  +ð	¢  +à	¢  +Ø	¢  +\r¢ü: û	    - ú	·  +Ð	¢  +ð	¢  +à	¢  +Ø	¢  +\r¢ü: ú	    - ù	·  +Ð	¢  +ð	¢  +à	¢  +Ø	¢  +\r¢ü: ù	    +ð	  +à	¢  +Ø	¢  +\r¢9È	  - û	·  +È	¢!ñD     ào@!òD        !ó   ñ ó òü: û	    - ú	·  +È	¢ ó òü: ú	    - ù	·  +È	¢ ó òü: ù	  - û	!)  - ú	!*Aÿ!+ )Aÿq!, *Aÿq!-Aÿ , -  - ù	 +Aÿq  +Ø!.  (\nA l  (¼j!/AÐ /Atj .6     (\nAj6\n      (´Aj6Ä	@@  (Ä	AÂHAqE\r  (Ä	·D      l@¡  (ô· D       @¢!ô  D      |@ ô£9¸	  A +à»c  +¸	  +¨¢ 9°	  A +è»c  +¸	  + ¢ 9¨	    +°	D      P@¢üA?q6¤	    +¨	D      P@¢üA?q6 	@@  +°	  +¨	AqE\r     ( 	At  (¤	jAtA Ý j( 6	    (	: 	    (	Av: 	    /	: 	  +¸	D      >À£!õDÍÌÌÌÌÌä?!ö   õ ö D      Ð? ö  +\r¢9	  - 	·  +	¢!÷D     ào@!øD        !ù   ÷ ù øü: 	    - 	·  +	¢ ù øü: 	    - 	·  +	¢ ù øü: 	    ( 	At  (¤	jAtA ß j( 6	    (	: 	    (	Av: 	    /	: 	  +¸	D      9À£!úD      ð?!û   ú û D333333Ó? û  +\r¢9	  +¨!ü  +è!ý    +   +à¢ ü ý¢ 9	    +	D¹?¢DÍÌÌÌÌÌì?   +	¢9	  - 	·  +	¢!þD     ào@!ÿD        !   þ  ÿü: 	    - 	·  +	¢  ÿü: 	    - 	·  +	¢  ÿü: 	  - 	!0  - 	!1Aÿ!2 0Aÿq!3 1Aÿq!4Aÿ 3 4  - 	 2Aÿq  +¸	!5  (Ä	A l  (¼j!6AÐ 6Atj 56     (Ä	Aj6Ä	    +Ø!  (¼!7Að»ã  7Atj 9     (¼Aj6¼    A 6ü@@  (üA ( ¼bHAqE\r  (ü!8  Aà¼â  8Atj6ø    (ø+ A +à»c¡9ð    (ø+A +è»c¡9è  A +à\\/9à  A +à\\89Ø  A +à\\8  + \r¢9Ð  A +à\\/  + \r¢9È  +Ð!  +Ø!  +à  +È¢  ¢ !  D      ð? £9À  +À!  +Ø!  +ð!     +à  +è¢  ¢ ¢9¸  +À!  +È!  +ð!     +Ð  +è¢  ¢ ¢9°@@  +°D333333Ó?eAqE\r     +¸  +°£D      ð? D      y@¢ü6¬  +ð!  +ð!    +è  +è¢  ¢ 9   +°!  D      >@ £ü6@  (AHAqE\r   (!9  A  9kAm6@@  (  (AmLAqE\r  (ô!:  Aá :k  (·DffffffÖ?¢üj  (j6@@@  (A HAq\r   (AÂNAqE\r  (!;  A  ;k6@@  (  (LAqE\r    (¬  (j6@@@  (A HAq\r   (A NAqE\r    (·  (·£9    (·  (·D      @££9ø  +!  +!    +ø  +ø¢  ¢ 9ð@  +ðD      ð?dAqE\r   (·D      l@¡  (ô· D       @¢!  D      |@ £9è@  +è  + Dé?¢dAqE\r   +°!  (!<@ Að»ã  <Atj+ fAqE\r   +ð!A !=   =+ð\\D      À¢ D       @¢ 8D¸ëQ¸¾?¢9à  +ð!   =+ð\\D      @¢ D      @¢ 8D{®Gáz´?¢9Ø  +à!D      ð?!       +Ø 9Ð     +ð¡9È  +ÐD      4@¢D      >@   +È¢  +\r¢!D     ào@!D        !     ü: Ç    +ÐD      I@¢D      Y@   +È¢  +\r¢  ü: Æ    +ÐD      D@¢D     f@   +È¢  +\r¢  ü: Å  +ð!  A +ð\\D      @¢ D      (@¢ A +à\\ 89¸@  +¸D333333ë?dAqE\r   +ðD333333Ó?cAqE\r     +¸D333333ë¿ D333333Ã?£9°  - Ç·  +°D      Y@¢ !D     ào@!D        !     ü: Ç    - Æ·  +°D     V@¢   ü: Æ    - Å·  +°D      N@¢   ü: Å@  +ðD333333ã?dAqE\r   +ðDffffffî?cAqE\r     +ðD333333ã¿ DffffffÖ?£9¨  +¨!    ¢9¨  - Ç·  +¨D      D@¢  +\r¢ !D     ào@!D        !       ü: Ç    - Æ·  +¨D      N@¢  +\r¢    ü: Æ    - Å·  +¨D      >@¢  +\r¢    ü: Å    (A l  (jAtAÐj( 6¤    (¤: £    (¤Av: ¢    /¦: ¡    +ÈD333333ë?¢9  - £!> >·!¡  - Ç >k·!¢  +!£ ¡ ¢ £¢ ü!?  - ¢!@ @· £  - Æ @k·¢ ü!A  - ¡!B B· £  - Å Bk·¢ ü!CAÿ!D ?Aÿq AAÿq CAÿq DAÿq  + !E  (A l  (j!FAÐ FAtj E6     (Aj6      (Aj6      (üAj6ü    A 6@@  (A (ðÏbHAqE\r  (!G@@AÐâ  GAlj(\r   (!H  AÐâ  HAlj+ A +à»c¡9  (!I  AÐâ  IAlj+A +è»c¡9  A +à\\/9ø  A +à\\89ð  A +à\\8  + \r¢9è  A +à\\/  + \r¢9à  +è!¤  +ð!¥  +ø  +à¢ ¤ ¥¢ !¦  D      ð? ¦£9Ø  +Ø!§  +ð!¨  +!©   §  +ø  +¢ ¨ ©¢ ¢9Ð  +Ø!ª  +à!«  +!¬   ª  +è  +¢ « ¬¢ ¢9È@  +ÈD333333Ó?eAqE\r     +Ð  +È£D      ð? D      y@¢ü6Ä  +!­  +!®    +  +¢ ­ ®¢ 9¸  +È!¯D      (@ ¯£ü!J  A J6´A<!KA!LAÜ!MAÿ!N   KAÿq LAÿq MAÿq NAÿq6 AÜ!OAÈ!PA(!QAÿ!R   OAÿq PAÿq QAÿq RAÿq6¤AÜ!SAÐ !TA !UAÿ!V   SAÿq TAÿq UAÿq VAÿq6¨Aÿ!WAÈ!XA(!Y   WAÿq XAÿq YAÿq WAÿq6¬A!ZAÐ ![A´!\\Aÿ!]   ZAÿq [Aÿq \\Aÿq ]Aÿq6A´!^A !_A!`Aÿ!a   ^Aÿq _Aÿq `Aÿq aAÿq6A´!bA(!cAø !dAÿ!e   bAÿq cAÿq dAÿq eAÿq6AÈ!fA !gA!hAÿ!i   fAÿq gAÿq hAÿq iAÿq6  (!jAÐâ  jAlj!kAÿ k- !l    A j lAtj( 6  (!mAÐâ  mAlj!nAÿ n- !o    Aj oAtj( 6A +ð\\!°    (·D      ø?¢ °D      @¢ 8D333333Ó?¢Dffffffæ? 9  (´!p  A  pk6ü@@  (ü  (´LAqE\r  (ô!q  Aá qk  (´·D333333Ó?¢üj  (üj6ø@@@  (øA HAq\r   (øAÂNAqE\r  (´!r  A  rk6ô@@  (ô  (´LAqE\r    (Ä  (ôj6ð@@@  (ðA HAq\r   (ðA NAqE\r    (ô  (ôl  (ü  (ülj·9è@  +è  (´·dAqE\r   +È!±  (ð!s@ ±Að»ã  sAtj+ fAqE\r   +!²  +è  (´·£!³   ²D      ð? ³¡¢¶8ä    *äC  HCü: ã@@  +è  (´·D      à?¢cAqE\r   (!t  (!t   t6Ü  (øA l  (ðjAtAÐj( !u  (Ü!vAÿ!w v wq¸!´  +!µ ´ µ¢ü!x µ w vAvq¸¢ü!y µ w vAvq¸¢ü!z xAÿq!{ yAÿq!| zAÿq!}Aÿ u { | }  - ã!~  (øA l  (ðj!AÐ Atj ~6     (ôAj6ô      (üAj6ü      (Aj6  @A (\\E\r   A +Ð\\A +à»c¡9Ð  A +Ø\\A +è»c¡9È  A +à\\/9À  A +à\\89¸  A +à\\8  + \r¢9°  A +à\\/  + \r¢9¨  +°!¶  +¸!·  +À  +¨¢ ¶ ·¢ !¸  D      ð? ¸£9   + !¹  +¸!º  +Ð!»   ¹  +À  +È¢ º »¢ ¢9  + !¼  +¨!½  +Ð!¾   ¼  +°  +È¢ ½ ¾¢ ¢9@  +D      à?dAqE\r     +  +£D      ð? D      y@¢ü6  +!¿D      |@ ¿£Dffffffæ?¢ü!  A 6    (Am6  +Ð!À  +È!Á   Á Á¢ À À¢ 9øA ! *\\!í *\\!î   î î í í»9ð@@  +ðD¹?dAqE\r A +ÀÀbD      $@¢8D      ð?¢!ÂA ·!Â   Â9è    +è  (·¢D{®Gáz´?¢9à    +è  (·¢D¸ëQ¸®?¢9Ø@@  +ðD¹?dAqE\r A +ÀÀbD      $@¢8  (·¢Dú~j¼t?¢!ÃA ·!Ã   Ã9ÐA¯!A!Aî !Aÿ!   Aÿq Aÿq Aÿq Aÿq6ÌAÃ!Aª!A!Aÿ!   Aÿq Aÿq Aÿq Aÿq6ÈAª!A!Aä !Aÿ!   Aÿq Aÿq Aÿq Aÿq6ÄA !A!Aä !Aÿ!   Aÿq Aÿq Aÿq Aÿq6ÀAø !Aß !A<!Aÿ!   Aÿq Aÿq Aÿq Aÿq6¼A¹!A!Aö !Aÿ!   Aÿq Aÿq Aÿq Aÿq6¸A!Aø !AÐ !Aÿ!   Aÿq Aÿq Aÿq Aÿq6´  (!  A  kAm6°@@  (°  (AmLAqE\r  (ô!  Aá k  (°j  +Ðüj6¬@@@  (¬A HAq\r   (¬AÂNAqE\r    (°·  (·D       @££9 @  + D333333Ã?dAqE\r   + Dffffffî?cAqE\r     (Am6    (  (Amk  +àüj6  (!   A   k6@@  (  (LAqE\r    (  (j  (Øj6@@@  (A HAq\r   (A NAq\r   +!Ä  (!¡ ÄAð»ã  ¡Atj+ fAqE\r@@  + Dé?dAqE\r   (¼!¢  (À!¢   ¢6@@  (A HAqE\r   (!£Aÿ!¤ £ ¤q¸!ÅD=\n×£p=ê?!Æ Å Æ¢ü!¥ Æ ¤ £Avq¸¢ü!¦ Æ ¤ £Avq¸¢ü!§Aÿ!¨   ¥Aÿq ¦Aÿq §Aÿq ¨Aÿq  +ø6    (  +ø6  (!©  (¬A l  (j!ªAÐ ªAtj ©6     (Aj6  @  + D333333Ã?dAqE\r   + Dffffffî?cAqE\r     (Am6    (  (Amj  +àük6  (!«  A  «k6@@  (  (LAqE\r    (  (j  (Øj6ü@@@  (üA HAq\r   (üA NAq\r   +!Ç  (ü!¬ ÇAð»ã  ¬Atj+ fAqE\r@@  + Dé?dAqE\r   (¼!­  (À!­   ­6ø@@  (A HAqE\r   (ø!®Aÿ!¯ ® ¯q¸!ÈD=\n×£p=ê?!É È É¢ü!° É ¯ ®Avq¸¢ü!± É ¯ ®Avq¸¢ü!²Aÿ!³   °Aÿq ±Aÿq ²Aÿq ³Aÿq  +ø6ø    (ø  +ø6ø  (ø!´  (¬A l  (üj!µAÐ µAtj ´6     (Aj6  @  + DffffffÖ¿dAqE\r   + D      Ð?cAqE\r     (Am6ô  (ô!¶  A  ¶k6ð@@  (ð  (ôLAqE\r    (  (ðj  (Øj6ì@@@  (ìA HAq\r   (ìA NAq\r   +!Ê  (ì!· ÊAð»ã  ·Atj+ fAqE\r@@  + D\n×£p=\nÇ?dAqE\r A!¸Aä !¹A2!ºAÿ!» ¸Aÿq ¹Aÿq ºAÿq »Aÿq!¼  (Ä!¼   ¼6è@@  (ðA HAqE\r   (è!½Aÿ!¾ ½ ¾q¸!ËD333333ë?!Ì Ë Ì¢ü!¿ Ì ¾ ½Avq¸¢ü!À Ì ¾ ½Avq¸¢ü!ÁAÿ!Â   ¿Aÿq ÀAÿq ÁAÿq ÂAÿq  +ø6è    (è  +ø6è  (è!Ã  (¬A l  (ìj!ÄAÐ ÄAtj Ã6     (ðAj6ð  @  + D333333Ó¿dAqE\r   + DÉ?cAqE\r     (Am6ä    (  (Amk  (äk  +Øük6à    +ØD333333Ó?¢ü6Ü  (ä!Å  A  Åk6Ø@@  (Ø  (äLAqE\r    (à  (Øj  (Øj6Ô    (¬  (Üj6Ð@@@  (ÔA HAq\r   (ÔA NAq\r   (ÐA HAq\r   (ÐAÂNAq\r   +!Í  (Ô!Æ ÍAð»ã  ÆAtj+ fAqE\r@@  + DÉ¿cAqE\r   (¸!Ç  (Ä!Ç   Ç6Ì  (Ì  +ø!È  (ÐA l  (Ôj!ÉAÐ ÉAtj È6     (ØAj6Ø  @  + D333333Ó¿dAqE\r   + DÉ?cAqE\r     (Am6È    (  (Amj  (Èj  +Øüj6Ä    +ØD333333Ó?¢ü6À  (È!Ê  A  Êk6¼@@  (¼  (ÈLAqE\r    (Ä  (¼j  (Øj6¸    (¬  (Àj6´@@@  (¸A HAq\r   (¸A NAq\r   (´A HAq\r   (´AÂNAq\r   +!Î  (¸!Ë ÎAð»ã  ËAtj+ fAqE\r@@  + DÉ¿cAqE\r   (¸!Ì  (Ä!Ì   Ì6°  (°  +ø!Í  (´A l  (¸j!ÎAÐ ÎAtj Í6     (¼Aj6¼  @  + DÍÌÌÌÌÌÜ¿cAqE\r     (Am6¬  (¬!Ï  A  Ïk6¨@@  (¨  (¬LAqE\r    (  (¨j  (Øj6¤@@@  (¤A HAq\r   (¤A NAq\r   +!Ï  (¤!Ð ÏAð»ã  ÐAtj+ fAqE\r    (Ì6 @  + DÍÌÌÌÌÌä¿dAqE\r   + Dá¿cAqE\r   (¨!Ñ ÑAu!Ò Ñ Òs Òk  (A\nmHAqE\r A(!ÓA!ÔA!ÕAÿ!Ö   ÓAÿq ÔAÿq ÕAÿq ÖAÿq6 @@  (¨A HAqE\r   ( !×Aÿ!Ø × Øq¸!ÐD)\\Âõ(ì?!Ñ Ð Ñ¢ü!Ù Ñ Ø ×Avq¸¢ü!Ú Ñ Ø ×Avq¸¢ü!ÛAÿ!Ü   ÙAÿq ÚAÿq ÛAÿq ÜAÿq  +ø6     (   +ø6   ( !Ý  (¬A l  (¤j!ÞAÐ ÞAtj Ý6     (¨Aj6¨  @  + Dö(\\Âõè¿dAqE\r   + DÃõ(\\Âå¿cAqE\r     (AmAj6  (!ß  A  ßk6@@  (  (LAqE\r    (  (j  (Øj6@@@  (A HAq\r   (A NAq\r   +!Ò  (!à ÒAð»ã  àAtj+ fAqE\r    (´6  (!á áAu!â@@ á âs âk  (FAqE\r   (!ãAÿ!ä ã äq¸!ÓDffffffæ?!Ô Ó Ô¢ü!å Ô ä ãAvq¸¢ü!æ Ô ä ãAvq¸¢ü!çAÿ!è   åAÿq æAÿq çAÿq èAÿq  +ø6    (  +ø6  (!é  (¬A l  (j!êAÐ êAtj é6     (Aj6  @  + Dffffffî¿dAqE\r   + Dö(\\Âõè¿cAqE\r     (Am6  (!ë  A  ëk6@@  (  (LAqE\r    (  (j  (Øj6@@@  (A HAq\r   (A NAq\r   +!Õ  (!ì ÕAð»ã  ìAtj+ fAqE\r  (´  +ø!í  (¬A l  (j!îAÐ îAtj í6     (Aj6      (°Aj6°      (Am6  (ô!ï  Aá ïk  (AmjAj6ü@  (üA NAqE\r   (üAÂHAqE\r   (!ð  A  ðk6ø@@  (ø  (LAqE\r    (  (øj  (Øj6ô@@@  (ôA HAq\r   (ôA NAqE\r  (ø·  (·£!Ö  D      ð? Ö¡9è  (üA l  (ôjAtAÐj( !ñ  +èD      N@¢ü!òA!óA!ôA\n!õ ñ óAÿq ôAÿq õAÿq òAÿq!ö  (üA l  (ôj!÷AÐ ÷Atj ö6     (øAj6ø    A 6ä@@  (äAÈHAqE\r  (ä!ø  AÀã  øAtj6à@@  (à*A ²_AqE\r     (à* »A +à»c¡9Ø    (à*»A +è»c¡9Ð  A +à\\/9È  A +à\\89À  A +à\\8  + \r¢9¸  A +à\\/  + \r¢9°  +¸!×  +À!Ø  +È  +°¢ × Ø¢ !Ù  D      ð? Ù£9¨  +¨!Ú  +À!Û  +Ø!Ü   Ú  +È  +Ð¢ Û Ü¢ ¢9   +¨!Ý  +°!Þ  +Ø!ß   Ý  +¸  +Ð¢ Þ ß¢ ¢9  (à!ùAÿ@@@ ù- E\r   (à!úAÿ ú- AFAqE\r    (à* ü   (Øj6    (à*ü   (Ôj6@  (A NAqE\r   (A HAqE\r   (A NAqE\r   (AÂHAqE\r   (à!û   û* û*8    *C  4Cü:     (à(Aÿq:     (à(AvAÿq:     (à(AvAÿq:   (A l  (j!üAÐ üAtj( !ý  - !þ  - !ÿ  - ! þAÿq! ÿAÿq! Aÿq!Aÿ ý     - !  (A l  (j!AÐ Atj 6 @  +DÉ?eAqE\r     +   +£D      ð? D      y@¢ü6  +Ø!à  +Ð!á   á á¢ à à¢ 9ø  +!âD       @ â£ü!  A 6ô  (à!   * *8ð    *ðC  \\Cü: ï    (à(Aÿq: î    (à(AvAÿq: í    (à(AvAÿq: ì  (ô!  A  k6è@@  (è  (ôLAqE\r  (ô!  A  k6ä@@  (ä  (ôLAqE\r@@  (ä  (äl  (è  (èlj  (ô  (ôlJAqE\r     (  (äj  (Øj6à  (ô!  Aá k  (èj  (Ôj6Ü@  (àA NAqE\r   (àA HAqE\r   (ÜA NAqE\r   (ÜAÂHAqE\r   (ÜA l  (àj!AÐ Atj( !  - î!  - í!  - ì! Aÿq! Aÿq! Aÿq!Aÿ      - ï!  (ÜA l  (àj!AÐ Atj 6     (äAj6ä      (èAj6è      (äAj6ä  A +ðícD¸ëQ¸? !ãA  ã9ðíc  Aõ6Ø@@  (ØAÂHAqE\r    (ØAák·D      l@£9Ð    +Ð  +Ð¢D      @¢9È@@  +ÈD333333Ó?cAqE\r   A 6Ä@@  (ÄA HAqE\r  (Ø·!ä  A +ðícD       @¢ äD{®Gáz´?¢   (Ä·D{®Gáz?¢ 8  +È¢9¸    (Ä·  +¸ ü6´@  (´A NAqE\r   (´A HAqE\r   (´  (ÄGAqE\r   (ØA l  (´j!  AÐ Atj( 6°  (°!  (ØA l  (Äj!AÐ Atj 6     (ÄAj6Ä      (ØAj6Ø  @A (\\E\r A +ØÀbD©?dAqE\r   A 6¬@@  (¬AÂHAqE\r  A 6¨@@  (¨A HAqE\r  (¨!A ! +ð\\!å  åD      I@¢üj  (¬ åD      >@¢üj¸!æD     ào@!ç   æ ç£9     (¬·D      |À£D333333Ó?¢D      ð? 9    +  +ØÀb¢DÙ?¢  +¢9   ç  +¢ü: Aÿ@  - AJAqE\r   (¬A l  (¨j!  AÐ Atj( 6  (!AÒ!A´!A! Aÿq! Aÿq!  Aÿq!¡Aÿ     ¡  - !¢  (¬A l  (¨j!£AÐ £Atj ¢6   (¬A l  (¨jAj!¤AÐ ¤Atj( !¥AÒ!¦A´!§A!¨ ¦Aÿq!© §Aÿq!ª ¨Aÿq!«Aÿ ¥ © ª «  - !¬  (¬A l  (¨jAj!­AÐ ­Atj ¬6   (¬AjA l  (¨j!®AÐ ®Atj( !¯AÒ!°A´!±A!² °Aÿq!³ ±Aÿq!´ ²Aÿq!µAÿ ¯ ³ ´ µ  - !¶  (¬AjA l  (¨j!·AÐ ·Atj ¶6   (¬AjA l  (¨jAj!¸AÐ ¸Atj( !¹AÒ!ºA´!»A!¼ ºAÿq!½ »Aÿq!¾ ¼Aÿq!¿Aÿ ¹ ½ ¾ ¿  - !À  (¬AjA l  (¨jAj!ÁAÐ ÁAtj À6     (¨Aj6¨      (¬Aj6¬    A 6@@  (A +ØÀbD      4@¢üHAqE\rA +ð\\!è    (A%l· èD      T@¢ üAÂo6A +ð\\!é    (AÉ l· éD      i@¢ üA o6|  A +ØÀbD      >@¢üA\nj6x  A 6t@  (t  (xH!ÂA !Ã ÂAq!Ä Ã!Å@ ÄE\r   (|  (tjA H!Å@ ÅAqE\r   (t·  (x·£!ê  D      ð? ê¡9h    +hA +ØÀb¢D      ^@¢ü: g  (A l  (|j  (tj!ÆAÐ ÆAtj( !ÇAÈ!ÈA¯!ÉAý !Ê ÈAÿq!Ë ÉAÿq!Ì ÊAÿq!ÍAÿ Ç Ë Ì Í  - g!Î  (A l  (|j  (tj!ÏAÐ ÏAtj Î6     (tAj6t    (Aj6    A6`  A6\\  (`!Ð  A  ÐkAk6X  A6T    (`·  (\\·£9H  A 6D@@  (D  (`HAqE\r  A 6@@@  (@  (`HAqE\rA!ÑA!ÒA\n!ÓA´!Ô ÑAÿq ÒAÿq ÓAÿq ÔAÿq!Õ  (T  (DjA l  (Xj  (@j!ÖAÐ ÖAtj Õ6     (@Aj6@      (DAj6D    A 6<@@  (<  (`HAqE\r  A 68@@  (8  (`HAqE\r  A +Ð\\  (\\·D       @£¡  (8·  +H£ ü64  A +Ø\\  (\\·D       @£¡  (<·  +H£ ü60@@@  (4A HAq\r   (4AÐ NAq\r   (0A HAq\r   (0AÐ NAqE\r  (4!×AÀ×â  ×AÐ lj  (0j!ØA !ÙAÿ@ Ø-   ÙAÿqGAq\r   (4A +Ð\\ük!Ú ÚAu!Û Ú Ûs Ûk!Ü  (0A +Ø\\ük!Ý ÝAu!Þ   Ü Ý Þs Þk9(@  +(D      @dAqE\r   (4AÐ l  (0jA â j-  !ß ßAK@@@@@@@@ ß A¾!àA¥!áAý !âAÿ!ã   àAÿq áAÿq âAÿq ãAÿq6$A¹!äA¥!åA!æAÿ!ç   äAÿq åAÿq æAÿq çAÿq6$A!èA!éAö !êAÿ!ë   èAÿq éAÿq êAÿq ëAÿq6$@@A +¨¼bD      à?dAqE\r A !ìA!íAÐ !îAÿ!ï ìAÿq íAÿq îAÿq ïAÿq!ðAä !ñAÈ !òA#!óAÿ!ô ñAÿq òAÿq óAÿq ôAÿq!ð   ð6$A(!õAî !öAÈ!÷Aÿ!ø   õAÿq öAÿq ÷Aÿq øAÿq6$Aî !ùAÐ !úA(!ûAÿ!ü   ùAÿq úAÿq ûAÿq üAÿq6$A´!ýA !þAø !ÿAÿ!   ýAÿq þAÿq ÿAÿq Aÿq6$  (4!AÀ×â  AÐ lj  (0j!A !Aÿ@ -   AÿqGAqE\r   (4A +Ð\\ük! Au!  s k!  (0A +Ø\\ük! Au!   s kD      @dAqE\r     ($: #    ($Av: "    /&: !  - #·!ëDÙ?!ì ë ì¢ü! ì  - "·¢ü! ì  - !·¢ü!Aÿ!   Aÿq Aÿq Aÿq Aÿq6$  ($!  (T  (<jA l  (Xj  (8j!AÐ Atj 6     (8Aj68      (<Aj6<      (`Am6    (`Am6  A~6@@  (ALAqE\r  A~6@@  (ALAqE\r@  (  (l  (  (ljALAqE\r Aÿ!A<! Aÿq Aÿq Aÿq Aÿq!  (T  (j  (jA l  (Xj  (j  (j!AÐ Atj 6     (Aj6      (Aj6    A6@@  (AHAqE\r    (A +à\\/  (·¢üj6    (A +à\\8  (·¢üj6@  (A NAqE\r   (  (`HAqE\r   (A NAqE\r   (  (`HAqE\r Aÿ!AÜ!A<! Aÿq Aÿq Aÿq Aÿq!  (T  (jA l  (Xj  (j!AÐ Atj 6     (Aj6    A 6 @@  (   (`HAqE\rAÚ !AÈ !A-!Aÿ! Aÿq Aÿq Aÿq Aÿq!  (TA l  (Xj  ( j!AÐ Atj 6 AÚ !AÈ !A-! Aÿ!¡ Aÿq Aÿq  Aÿq ¡Aÿq!¢  (T  (`jAkA l  (Xj  ( j!£AÐ £Atj ¢6 AÚ !¤AÈ !¥A-!¦Aÿ!§ ¤Aÿq ¥Aÿq ¦Aÿq §Aÿq!¨  (T  ( jA l  (Xj!©AÐ ©Atj ¨6 AÚ !ªAÈ !«A-!¬Aÿ!­ ªAÿq «Aÿq ¬Aÿq ­Aÿq!®  (T  ( jA l  (Xj  (`jAk!¯AÐ ¯Atj ®6     ( Aj6     A°\rj"±#K ±#Ir@ ±  ±$ Á}# Ak!   "#K #Ir@   $ A ! *\\! *\\!      »9 @@  + D¹?cAqE\r   A 6  A +ÀÀbD      $@¢8D      @¢  + ¢ü6  (!  Aj"#K #Ir@   $  ²# A k!   9  9  +ü6  +ü6 (A (Ä¼bJ!A ! Aq! !@ E\r  (A (Ì¼bH!A ! Aq!	 ! 	E\r  (A (È¼bJ!\nA ! \nAq! ! E\r  (A (Ð¼bH! AqÊ|# A k! "#K #Ir@   $    6  9 +D      °?¢!D      ð?!  D         9 +!   ¢9  (:   (Av:   /:  - !  ·AÚ k· +¢ ü:  - !  ·AÄ k· +¢ ü:  - !  ·A k· +¢ ü:  - ! - !Aÿ! Aÿq!	 Aÿq!\nAÿ 	 \n -  Aÿq! A j"\r#K \r#Ir@ \r  \r$  8# A k!   9  9  9 + + +¡ +¢ å\r|# A k! "\r#K \r#Ir@ \r  \r$    6  6  (Av: Aÿ@@ - \r   (6  - ·D     ào@£9  (:  (!A!   v:   /:   (:   ( v:   /:  - ! ·! -  k·! +!   ¢ ü! - ! ·  -  k·¢ ü! - !	 	·  -  	k·¢ ü!\nAÿ!  Aÿq Aÿq \nAÿq Aÿq6 (! A j"#K #Ir@   $  ?|# Ak!   9 + +¢! +!   ! D      @ ¡¢C# Ak!   6  6@@ ( (JAqE\r  (! (! a|# Ak! "#K #Ir@   $    6  6 (· (·)! Aj"#K #Ir@   $   AÐ\n A +À\n A +Ð\\\n A +Ø\\\n A +à\\\n A (è\\\n A +ð\\*# Ak!   A (ø\\6A !A  6ø\\  (\n A (ü\\\n A (\\A (\\! A  k!A  6\\\n A (\\\n A (\\S}# Ak!   8  8  8 *!A  8\\ *!A  8\\ *!A  8\\# Ak!   A 6A _|# Ak! "#K #Ir@   $    9  9  + + 1! Aj"#K #Ir@   $   (|D      ð?    ¢"D      à?¢"¡"D      ð? ¡ ¡    DË ú>¢DwQÁlÁV¿ ¢DLUUUUU¥? ¢  ¢" ¢  DÔ8¾éú¨½¢DÄ±´½î!> ¢D­RO~¾ ¢ ¢   ¢¡  |# A°k""#K #Ir@   $  A}jAm"A  A J"Ahl j!@ AtA j( "	 Aj"\njA H\r  	 j!  \nk!A !@@@ A N\r D        ! At(°·! AÀj Atj 9  Aj! Aj" G\r  Ahj!A ! 	A  	A J!\r AH!@@@ E\r D        !  \nj!A !D        !@   Atj+  AÀj  kAtj+ ¢  ! Aj" G\r   Atj 9   \rF! Aj! E\r A/ k!A0 k! AtA°j! 	!@@  Atj+ !A ! !@ AH\r @ Aàj Atj D      p>¢ü·"D      pÁ¢  ü6   AtjAxj+   ! Aj! Aj" G\r   7!  D      À?¢0D       À¢ " ü"·¡!@@@@@ AH"\r  Aàj AtjA|j" ( "  u" tk"6   u!  j! \r Aàj AtjA|j( Au! AH\rA! D      à?f\r A !A !A !\rA!@ AH\r @ Aàj Atj"\n( !@@@@ \rE\r Aÿÿÿ!\r E\rA!\r \n \r k6 A!\rA !A !\rA! Aj" G\r @ \r Aÿÿÿ!@@ Aj Aÿÿÿ! Aàj AtjA|j"\r \r(  q6  Aj! AG\r D      ð? ¡!A! \r  D      ð? 7¡!@ D        b\r A ! !@  	L\r @ Aàj Aj"Atj(  r!  	J\r  E\r @ Ahj! Aàj Aj"Atj( E\r  A!@ "Aj! Aàj 	 kAtj( E\r   j!\r@ AÀj  j"Atj  Aj"Atj( ·9 A !D        !@ AH\r @   Atj+  AÀj  kAtj+ ¢  ! Aj" G\r   Atj 9   \rH\r  \r!@@ A k7"D      pAfE\r  Aàj Atj D      p>¢ü"·D      pÁ¢  ü6  Aj! ! ü! Aàj Atj 6 D      ð? 7!@ A H\r  !@  "Atj  Aàj Atj( ·¢9  Aj! D      p>¢! \r  !\r@@@ 	  \rk" 	 H"A N\r D        !  \rAtj! A !D        !@ At"+   j+ ¢  !  G! Aj! \r  A j Atj 9  \rA J! \rAj!\r \r @@@@@  D        !@ A L\r  !@ A j Atj"Axj" + " + " "9     ¡ 9  AK! Aj! \r  AF\r  !@ A j Atj"Axj" + " + " "9     ¡ 9  AK! Aj! \r D        !@  A j Atj+  ! AK! Aj! \r  + ! \r  9  +¨!  9  9D        !@ A H\r @ "Aj!  A j Atj+  ! \r     9 D        !@ A H\r  !@ "Aj!  A j Atj+  ! \r     9  +  ¡!A!@ AH\r @  A j Atj+  !  G! Aj! \r     9  9  +¨!  9  9 A°j"#K #Ir@   $  AqÎ\n~|# A0k""#K #Ir@   $ @@@@  ½"\nB §"Aÿÿÿÿq"AúÔ½K\r  Aÿÿ?qAûÃ$F\r@ Aü²K\r @ \nB S\r    D  @Tû!ù¿ " D1cba´Ð½ "9     ¡D1cba´Ð½ 9A!   D  @Tû!ù? " D1cba´Ð= "9     ¡D1cba´Ð= 9A!@ \nB S\r    D  @Tû!	À " D1cba´à½ "9     ¡D1cba´à½ 9A!   D  @Tû!	@ " D1cba´à= "9     ¡D1cba´à= 9A~!@ A»ñK\r @ A¼û×K\r  Aü²ËF\r@ \nB S\r    D  0|ÙÀ " DÊ§é½ "9     ¡DÊ§é½ 9A!   D  0|Ù@ " DÊ§é= "9     ¡DÊ§é= 9A}! AûÃäF\r@ \nB S\r    D  @Tû!À " D1cba´ð½ "9     ¡D1cba´ð½ 9A!   D  @Tû!@ " D1cba´ð= "9     ¡D1cba´ð= 9A|! AúÃäK\r  DÈÉm0_ä?¢D      8C D      8Ã "ü!@@   D  @Tû!ù¿¢ " D1cba´Ð=¢"\r¡"D-DTû!é¿cE\r  Aj! D      ð¿ "D1cba´Ð=¢!\r   D  @Tû!ù¿¢ ! D-DTû!é?dE\r  Aj! D      ð? "D1cba´Ð=¢!\r   D  @Tû!ù¿¢ !   \r¡" 9 @ Av"  ½B4§AÿqkAH\r    D  `a´Ð=¢" ¡" Dsp.£;¢  ¡  ¡¡"\r¡" 9 @   ½B4§AÿqkA2N\r  !   D   .£;¢" ¡" DÁI %{9¢  ¡  ¡¡"\r¡" 9     ¡ \r¡9@ AÀÿI\r      ¡" 9    9A ! AjAr! \nBÿÿÿÿÿÿÿB°Á ¿!  Aj!A!@   ü·"9    ¡D      pA¢!  Aq!A ! ! \r    9 A!@ "Aj! Aj Atj+ D        a\r  Aj  AvAêwj AjA,! + ! @ \nBU\r    9   +9A  k!   9   +9 A0j"	#K 	#Ir@ 	  	$  |    ¢"  ¢¢ D|ÕÏZ:Ùå=¢Dë+æåZ¾ ¢  D}þ±WãÇ>¢DÕaÁ *¿ ¢D¦ø?  !   ¢!@ \r    ¢DIUUUUUÅ¿ ¢       D      à?¢  ¢¡¢ ¡ DIUUUUUÅ?¢ ¡÷|# Ak""#K #Ir@   $ @@  ½B §Aÿÿÿÿq"AûÃ¤ÿK\r D      ð?! AÁòI\r  D        +!@ AÀÿI\r     ¡!   -! +!  + !@@@@ Aq     +!   A.!   +!   A.! Aj"#K #Ir@   $     K @  2Bÿÿÿÿÿÿÿÿÿ Bøÿ V\r      ¥ 2Bÿÿÿÿÿÿÿÿÿ Bøÿ V!    ½ ~@@ ½"B"P\r  4Bÿÿÿÿÿÿÿÿÿ Bøÿ V\r   ½"B4§Aÿq"AÿG\r   ¢" £@ B" V\r   D        ¢    Q B4§Aÿq!@@ \r A !@ B"B S\r @ Aj! B"BU\r  A k­! BÿÿÿÿÿÿÿB!@@ \r A !@ B"B S\r @ Aj! B"BU\r  A k­! BÿÿÿÿÿÿÿB!@  L\r @@  }"B S\r  ! B R\r   D        ¢ B! Aj" J\r  !@  }"B S\r  ! B R\r   D        ¢@ BÿÿÿÿÿÿÿV\r @ Aj! "B! BT\r  B!@@ AH\r  Bx| ­B4! A k­!  ¿   ½ A   Aj­7øíc)~A A )øícB­þÕäÔý¨Ø ~B|" 7øíc  B!§® @@ AH\r   D      à¢! @ AÿO\r  Axj!  D      à¢!  Aý AýIApj! AxJ\r   D      `¢! @ A¸pM\r  AÉj!  D      `¢!  Aðh AðhKAj!   Aÿj­B4¿¢î|# Ak""#K #Ir@   $ @@  ½B §Aÿÿÿÿq"AûÃ¤ÿK\r  AÀòI\r  D        A .! @ AÀÿI\r     ¡!    -! +!  + !@@@@ Aq     A.!    +!    A.!    +!  Aj"#K #Ir@   $   ®~|@@  ½"Bÿÿÿÿ Bðåò?T"E\r D-DTû!é?  ¡D\\3&¦<   BU"¡ ! D        !        ¢"¢"DcUUUUUÕ?¢    ¢"    DsS`ÛËuó¾¢D¦7 ~? ¢DeòòØDC? ¢D(VÉ"mm? ¢D7Öôd? ¢DzþÁ?       DÔz¿tp*û>¢Dé§ð2¸? ¢Dh÷&0? ¢DàþÈÛW? ¢Dnéã&? ¢DþA³º¡«? ¢ ¢  ¢   " !@ \r A Atk·"     ¢   £¡ "  ¡"  Aq@ E\r D      ð¿ £" ½Bp¿"  ½Bp¿"  ¡¡¢  ¢D      ð?  ¢  ! ­# Ak""#K #Ir@   $ @@  ½B §Aÿÿÿÿq"AûÃ¤ÿK\r  AòI\r  D        A 9! @ AÀÿI\r     ¡!    -! +  + Aq9!  Aj"#K #Ir@   $    A$A AjApq$ # #k # #  "#K #Ir@   $ &#   kApq""#K #Ir@   $   #    Aîã BAîã 	 Aîã Cà@  \r A !@A (îcE\r A (îcF!@A (îcE\r A (îcF r!@D( " E\r @@  (  (F\r   F r!  (8" \r E @  (  (F\r   A A   ($   (\r A@  ("  ("F\r     k¬A  ((   A 6  B 7  B 7A \n   $ $à AÀ      A         A   A   A         7         (   D   (      2   2   7         <         <   7   >   (   \n   (   F   \n      F   7   #   #   -   -               ù¢ DNn ü) ÑW\' Ý4õ bÛÀ < AC cQþ »Þ« ·aÅ :n$ ÒMB Ià 	ê. Ñ ëþ )± è>§ õ5 D». é ´&p A~_ Ö9 S9 ô9 _ (ù½ ø; Þÿ  /ï \nZ mm Ï~6 	Ë\' FO· f? -ê_ º\'u åëÇ ={ñ ÷9 R ûkê ±_ ] 0V {üF ð«k  ¼Ï 6ô ã© ^a æ e  _ @h Øÿ \'sM 1 ÊV É¨s {â` kÀ ÄG ÍgÃ 	èÜ Y* vÄ ¦ D¯Ý WÑ ¥> ÿ 3~? Â2è OÞ »}2 &=Ã kï ø^ 5: òÊ ñ |! j$| Õnú 0-w ;C µÆ Ã ­ÄÂ ,MA  ] }F ãq- Æ 3b  ´Ò| ´§ 7UÕ ×>ö £ Mvü d* p×« c|ø z°W ç ÀIV ;ÖÙ §8 $#Ë Öw ZT#  ¹ ñ\n Îß 1ÿ fj Wa ¬ûG ~Ø "e· 2è æ¿` ïÄÍ l6	 ]?Ô Þ× X;Þ Þ Ò"( (è âXM ÆÊ2 ã à}Ë ÀP ó§ à[ .4 b H õ[ ­° éò HJC gÓ ªÝØ ®_B jaÎ \n(¤ Ó´ ¦ò \\w £Â a< sx ¯Z o×½ -¦c ô¿Ë ï &Ág UÊE ÊÙ6 (¨Ò Âa Éw & F ÄYÄ ÈÅD M²  ó ÔC­ )Iå ýÕ  ¾ü Ì pÎî >õ ìñ ³çÃ Çø(  Áq> .	³ Eó  « { .µ GÂ {2/ Um r§ kç 1Ë yJ Ayâ ôß è âæ 1 ík __6 »ý H´ g¤l qrB ]2 ¸ ¼å	 1% ÷t9 0 \r Kh ,îX Gª tç ½Ö$ ÷}¦ nHr ï ¦ ´ö ÑSQ Ï\nò  3 õK~ ²ch Ý>_ @]  UR) 7dÀ mØ 2H2 [Lu NqÔ ETn 	Á *õi fÕ \' ]P ´;Û êvÅ ù Ik} \'º i) ÆÌ¬ ­T âj Ù ,rP ¤¾ w ó0p  ü\' êq¨ fÂI dà= Ý £? Cý \r 1AÞ 9 Ýp ·ç ß; 7+ \\  Z  èØ l¯ ÛÿK 8 Yv b¥ aË» Ç¹ @½ Òò Iu\' ë¶ö Û"» \nª &/ dv 	;3  Q:ª £Â ¯í® \\& mÂM -z ÀV ? 	ðö +@ m1 9´   ØÃ[ õÄ Æ­K NÊ¥ §7Í æ©6 « ÝBh cÞ vï hR üÛ7 ®¡« ß1  ®¡ ûÚ dMf í· )e0 WV¿ Gÿ: jù¹ u¾ó (ß «0 fö Ë ú" Ùä =³¤ W 6Í	 NBé ¾¤ 3#µ ðª Oe¨ ÒÁ¥ ? [xÍ #ùv { r Æ¦S onâ ïë  JX ÄÚ· ªfº vÏÏ Ñ ±ñ- Á Ã­w HÚ ÷]  Æô ¬ð/ Ýì ?\\¼ ÐÞm Ç *Û¶ £%:  ¯ ­S ¶W )-´ K~ Ú§ vª {Y¡ * Ü·- úåý Ûþ ¾ý ävl ©ü >p n ýÿ (> ag3 * M½ê ³ç¯ mn g9 1¿[ ×H 0ß Ç-C %a5 ÉpÎ 0Ë¸ ¿lý ¤ ¢ lä ZÝ  !oG bÒ ¹\\ paI kVà R PU7 Õ· 3ñÄ n_ ]0ä .© ²Ã ¡26 ·¤ ê±Ô ÷! iä \'ÿw  @- OÍ   ¥ ³¢Ó /]\n ´ùB ÚË }¾Ð ÛÁ «½ Ê¢ j\\ .U \' U ð á d A ¾Þ Úý* k%¶ {4 óþ ¹¿ hjO J*¨ OÄZ -ø¼ ×Z ôÇ \rM  :¦ ¤W_ ?± 8 Ì  qÝ ÉÞ¶ ¿`õ Me k °¬ ²ÀÐ QUH û rÃ £; À@5 Ü{ àEÌ N)ú ÖÊÈ èóA |dÞ dØ Ù¾1 ¤Ã wXÔ iãÅ ðÚ º:< FF Uu_ Ò½õ nÆ ¬.] Dí >B aÄ )ýé çÖó "|Ê o5 àÅ ÿ× njâ °ýÆ Á |]t k­² Ín >r{ Æj ÷Ï© )sß µÉº · Q â²\r tº$ å}` tØ \r,  ~f ) zv ýý¾ VEï Ù~6 ìÙ º¹ Äü 1¨\' ñnÃ Å6 Ø¨V ´¨µ ÏÌ - oW4 ,V Îã Ö ¹ k^ª >* _Ì ýJ áôû ;m â, éÔ ü´© ïîÑ .5É /9a 8!D ÙÈ ü\n ûJj /Ø S´ N T"Ì *UÜ ÀÆÖ  p¸ id &Z` ?Rî  ôµ üËõ 4¼- 4¼î è]Ì Ý^` g 3ï É¸ aX áW¼ QÆ Ø> ÝqH -Ý ¯¡ !,F Yó× Ùz TÀ Oú Vü åy® "6 8­" gÜ Uèª &8 Êç Q\r¤ 3± ©× iH e²ð § L ùÑ6 !³ {J Ï! @Ü ÜGU át: gëB þß ^Ô_ {g¤ º¬z Uö¢ +# AºU Yn !* 9G ãæ åÔ Iû@ ÿVé Ê ÅY ú+ ÓÁÅ ÅÏ ÛZ® GÅ Cb !; ,y a *L{ , C¿ & x< ¨Ää åÛ{ Ä:Â &ôê ÷g \r¿ e£+ =± ½| ¤QÜ \'Ýc iáÝ  ¨) hÎ( 	í´ D  NÊ pc ~|# ¹2 §õ Vç !ñ µ* o~M ¥Q µù« ßÖ Ýa 6 Ä: ¢¡ rím 9z ¸© k2\\ F\'[  4í Ò w üôU YM àq            @û!ù?    -Dt>   Fø<   `QÌx;   ð9   @ %z8   "ã6    ói5 AÀ      Y@      >@ target_features+mutable-globals+nontrapping-fptoint+bulk-memory+sign-ext+reference-types+\nmultivalue+bulk-memory-opt+call-indirect-overlong');
}

function getBinarySync(file) {
  return file;
}

async function getWasmBinary(binaryFile) {

  // Otherwise, getBinarySync should be able to get it synchronously
  return getBinarySync(binaryFile);
}

async function instantiateArrayBuffer(binaryFile, imports) {
  try {
    var binary = await getWasmBinary(binaryFile);
    var instance = await WebAssembly.instantiate(binary, imports);
    return instance;
  } catch (reason) {
    err(`failed to asynchronously prepare wasm: ${reason}`);

    abort(reason);
  }
}

async function instantiateAsync(binary, binaryFile, imports) {
  return instantiateArrayBuffer(binaryFile, imports);
}

function getWasmImports() {
  // prepare imports
  var imports = {
    'env': wasmImports,
    'wasi_snapshot_preview1': wasmImports,
  };
  return imports;
}

// Create the wasm instance.
// Receives the wasm imports, returns the exports.
async function createWasm() {
  // Load the wasm module and create an instance of using native support in the JS engine.
  // handle a generated wasm instance, receiving its exports and
  // performing other necessary setup
  function receiveInstance(instance) {
    wasmExports = instance.exports;

    assignWasmExports(wasmExports);

    updateMemoryViews();

    return wasmExports;
  }

  // Prefer streaming instantiation if available.
  // Async compilation can be confusing when an error on the page overwrites Module
  // (for example, if the order of elements is wrong, and the one defining Module is
  // later), so we save Module and check it later.
  var trueModule = Module;
  function receiveInstantiationResult(result) {
    // 'result' is a ResultObject object which has both the module and instance.
    // receiveInstance() will swap in the exports (to Module.asm) so they can be called
    assert(Module === trueModule, 'the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?');
    trueModule = null;
    // TODO: Due to Closure regression https://github.com/google/closure-compiler/issues/3193, the above line no longer optimizes out down to the following line.
    // When the regression is fixed, can restore the above PTHREADS-enabled path.
    return receiveInstance(result['instance']);
  }

  var info = getWasmImports();

  // User shell pages can write their own Module.instantiateWasm = function(imports, successCallback) callback
  // to manually instantiate the Wasm module themselves. This allows pages to
  // run the instantiation parallel to any other async startup actions they are
  // performing.
  // Also pthreads and wasm workers initialize the wasm instance through this
  // path.
  var instantiateWasm = Module['instantiateWasm'];
  if (instantiateWasm) {
    return new Promise((resolve) => {
      try {
        instantiateWasm(info, (inst) => resolve(receiveInstance(inst)));
      } catch(e) {
        err(`Module.instantiateWasm callback failed with error: ${e}`);
        throw e;
      }
    });
  }

  wasmBinaryFile ??= findWasmBinary();
  var result = await instantiateAsync(wasmBinary, wasmBinaryFile, info);
  var exports = receiveInstantiationResult(result);
  return exports;
}

// end include: preamble.js

// Begin JS library code


  class ExitStatus {
      name = 'ExitStatus';
      constructor(status) {
        this.message = `Program terminated with exit(${status})`;
        this.status = status;
      }
    }

  /** @type {!Int32Array} */
  var HEAP32;

  /** @type {!Int8Array} */
  var HEAP8;

  /** @type {!Uint32Array} */
  var HEAPU32;

  var callRuntimeCallbacks = (callbacks) => {
      while (callbacks.length > 0) {
        // Pass the module as the first argument.
        callbacks.shift()(Module);
      }
    };
  var onPostRuns = [];
  var addOnPostRun = (cb) => onPostRuns.push(cb);

  var onPreRuns = [];
  var addOnPreRun = (cb) => onPreRuns.push(cb);


  var noExitRuntime = true;

  function ptrToString(ptr) {
      assert(typeof ptr === 'number', `ptrToString expects a number, got ${typeof ptr}`);
      // Convert to 32-bit unsigned value
      ptr >>>= 0;
      return '0x' + ptr.toString(16).padStart(8, '0');
    }

  var setStackLimits = () => {
      var stackLow = _emscripten_stack_get_base();
      var stackHigh = _emscripten_stack_get_end();
      ___set_stack_limits(stackLow, stackHigh);
    };

  var stackRestore = (val) => __emscripten_stack_restore(val);

  var stackSave = () => _emscripten_stack_get_current();

  var warnOnce = (text) => {
      warnOnce.shown ||= {};
      if (!warnOnce.shown[text]) {
        warnOnce.shown[text] = 1;
        if (ENVIRONMENT_IS_NODE) text = 'warning: ' + text;
        err(text);
      }
    };

  

  
  
  var ___handle_stack_overflow = (requested) => {
      var base = _emscripten_stack_get_base();
      var end = _emscripten_stack_get_end();
      abort(`stack overflow (Attempt to set SP to ${ptrToString(requested)}` +
            `, with stack limits [${ptrToString(end)} - ${ptrToString(base)}` +
            ']). If you require more stack space build with -sSTACK_SIZE=<bytes>');
    };

  
  var runtimeKeepaliveCounter = 0;
  var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;
  var _proc_exit = (code) => {
      EXITSTATUS = code;
      if (!keepRuntimeAlive()) {
        Module['onExit']?.(code);
        ABORT = true;
      }
      quit_(code, new ExitStatus(code));
    };
  
  
  /** @param {boolean|number=} implicit */
  var exitJS = (status, implicit) => {
      EXITSTATUS = status;
  
      checkUnflushedContent();
  
      // if exit() was called explicitly, warn the user if the runtime isn't actually being shut down
      if (keepRuntimeAlive() && !implicit) {
        var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
        err(msg);
      }
  
      _proc_exit(status);
    };

  var handleException = (e) => {
      // Certain exception types we do not treat as errors since they are used for
      // internal control flow.
      // 1. ExitStatus, which is thrown by exit()
      // 2. "unwind", which is thrown by emscripten_unwind_to_js_event_loop() and others
      //    that wish to return to JS event loop.
      if (e instanceof ExitStatus || e == 'unwind') {
        return EXITSTATUS;
      }
      checkStackCookie();
      if (e instanceof WebAssembly.RuntimeError) {
        if (_emscripten_stack_get_current() <= 0) {
          err('Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 65536)');
        }
      }
      quit_(1, e);
    };

  var getCFunc = (ident) => {
      var func = Module['_' + ident]; // closure exported function
      assert(func, `Cannot call unknown function ${ident}, make sure it is exported`);
      return func;
    };
  
  var writeArrayToMemory = (array, buffer) => {
      assert(array.length >= 0, 'writeArrayToMemory array must have a length (should be an array or typed array)')
      HEAP8.set(array, buffer);
    };
  
  var lengthBytesUTF8 = (str) => {
      var len = 0;
      for (var i = 0; i < str.length; ++i) {
        // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code
        // unit, not a Unicode code point of the character! So decode
        // UTF16->UTF32->UTF8.
        // See http://unicode.org/faq/utf_bom.html#utf16-3
        var c = str.charCodeAt(i); // possibly a lead surrogate
        if (c <= 0x7F) {
          len++;
        } else if (c <= 0x7FF) {
          len += 2;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
          len += 4; ++i;
        } else {
          len += 3;
        }
      }
      return len;
    };
  
  var stringToUTF8Array = (str, heap, outIdx, maxBytesToWrite) => {
      assert(typeof str === 'string', `stringToUTF8Array expects a string (got ${typeof str})`);
      // Parameter maxBytesToWrite is not optional. Negative values, 0, null,
      // undefined and false each don't write out any bytes.
      if (!(maxBytesToWrite > 0))
        return 0;
  
      var startIdx = outIdx;
      var endIdx = outIdx + maxBytesToWrite - 1; // -1 for string null terminator.
      for (var i = 0; i < str.length; ++i) {
        // For UTF8 byte structure, see http://en.wikipedia.org/wiki/UTF-8#Description
        // and https://www.ietf.org/rfc/rfc2279.txt
        // and https://tools.ietf.org/html/rfc3629
        var u = str.codePointAt(i);
        if (u <= 0x7F) {
          if (outIdx >= endIdx) break;
          heap[outIdx++] = u;
        } else if (u <= 0x7FF) {
          if (outIdx + 1 >= endIdx) break;
          heap[outIdx++] = 0xC0 | (u >> 6);
          heap[outIdx++] = 0x80 | (u & 63);
        } else if (u <= 0xFFFF) {
          if (outIdx + 2 >= endIdx) break;
          heap[outIdx++] = 0xE0 | (u >> 12);
          heap[outIdx++] = 0x80 | ((u >> 6) & 63);
          heap[outIdx++] = 0x80 | (u & 63);
        } else {
          if (outIdx + 3 >= endIdx) break;
          if (u > 0x10FFFF) warnOnce(`Invalid Unicode code point ${ptrToString(u)} encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).`);
          heap[outIdx++] = 0xF0 | (u >> 18);
          heap[outIdx++] = 0x80 | ((u >> 12) & 63);
          heap[outIdx++] = 0x80 | ((u >> 6) & 63);
          heap[outIdx++] = 0x80 | (u & 63);
          // Gotcha: if codePoint is over 0xFFFF, it is represented as a surrogate pair in UTF-16.
          // We need to manually skip over the second code unit for correct iteration.
          i++;
        }
      }
      // Null-terminate the pointer to the buffer.
      heap[outIdx] = 0;
      return outIdx - startIdx;
    };
  
  /** @type {!Uint8Array} */
  var HEAPU8;
  var stringToUTF8 = (str, outPtr, maxBytesToWrite) => {
      assert(typeof maxBytesToWrite == 'number', 'stringToUTF8 requires a third parameter that specifies the length of the output buffer');
      return stringToUTF8Array(str, HEAPU8, outPtr, maxBytesToWrite);
    };
  
  var stackAlloc = (sz) => __emscripten_stack_alloc(sz);
  var stringToUTF8OnStack = (str) => {
      var size = lengthBytesUTF8(str) + 1;
      var ret = stackAlloc(size);
      stringToUTF8(str, ret, size);
      return ret;
    };
  
  
  
  
  var UTF8Decoder = globalThis.TextDecoder && new TextDecoder();
  
  
    /**
   * heapOrArray is either a regular array, or a JavaScript typed array view.
   * @param {number} idx
   * @param {number=} maxBytesToRead
   * @param {boolean=} ignoreNul
   * @return {number}
   */
  var findStringEnd = (heapOrArray, idx, maxBytesToRead, ignoreNul) => {
      var maxIdx = idx + maxBytesToRead;
      if (ignoreNul) return maxIdx;
      // TextDecoder needs to know the byte length in advance, it doesn't stop on
      // null terminator by itself.
      // As a tiny code save trick, compare idx against maxIdx using a negation,
      // so that maxBytesToRead=undefined/NaN means Infinity.
      while (heapOrArray[idx] && !(idx >= maxIdx)) ++idx;
      return idx;
    };
  
  
    /**
   * Given a pointer 'idx' to a null-terminated UTF8-encoded string in the given
   * array that contains uint8 values, returns a copy of that string as a
   * Javascript String object.
   * heapOrArray is either a regular array, or a JavaScript typed array view.
   * @param {number=} idx
   * @param {number=} maxBytesToRead
   * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
   * @return {string}
   */
  var UTF8ArrayToString = (heapOrArray, idx = 0, maxBytesToRead, ignoreNul) => {
  
      var endPtr = findStringEnd(heapOrArray, idx, maxBytesToRead, ignoreNul);
  
      // When using conditional TextDecoder, skip it for short strings as the overhead of the native call is not worth it.
      if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
        return UTF8Decoder.decode(heapOrArray.subarray(idx, endPtr));
      }
      var str = '';
      while (idx < endPtr) {
        // For UTF8 byte structure, see:
        // http://en.wikipedia.org/wiki/UTF-8#Description
        // https://www.ietf.org/rfc/rfc2279.txt
        // https://tools.ietf.org/html/rfc3629
        var u0 = heapOrArray[idx++];
        if (!(u0 & 0x80)) { str += String.fromCharCode(u0); continue; }
        var u1 = heapOrArray[idx++] & 63;
        if ((u0 & 0xE0) == 0xC0) { str += String.fromCharCode(((u0 & 31) << 6) | u1); continue; }
        var u2 = heapOrArray[idx++] & 63;
        if ((u0 & 0xF0) == 0xE0) {
          u0 = ((u0 & 15) << 12) | (u1 << 6) | u2;
        } else {
          if ((u0 & 0xF8) != 0xF0) warnOnce(`Invalid UTF-8 leading byte ${ptrToString(u0)} encountered when deserializing a UTF-8 string in wasm memory to a JS string!`);
          u0 = ((u0 & 7) << 18) | (u1 << 12) | (u2 << 6) | (heapOrArray[idx++] & 63);
        }
  
        if (u0 < 0x10000) {
          str += String.fromCharCode(u0);
        } else {
          var ch = u0 - 0x10000;
          str += String.fromCharCode(0xD800 | (ch >> 10), 0xDC00 | (ch & 0x3FF));
        }
      }
      return str;
    };
  
  
    /**
   * Given a pointer 'ptr' to a null-terminated UTF8-encoded string in the
   * emscripten HEAP, returns a copy of that string as a Javascript String object.
   *
   * @param {number} ptr
   * @param {number=} maxBytesToRead - An optional length that specifies the
   *   maximum number of bytes to read. You can omit this parameter to scan the
   *   string until the first 0 byte. If maxBytesToRead is passed, and the string
   *   at [ptr, ptr+maxBytesToReadr[ contains a null byte in the middle, then the
   *   string will cut short at that byte index.
   * @param {boolean=} ignoreNul - If true, the function will not stop on a NUL character.
   * @return {string}
   */
  var UTF8ToString = (ptr, maxBytesToRead, ignoreNul) => {
      assert(typeof ptr == 'number', `UTF8ToString expects a number (got ${typeof ptr})`);
      return ptr ? UTF8ArrayToString(HEAPU8, ptr, maxBytesToRead, ignoreNul) : '';
    };
  
    /**
   * @param {string|null=} returnType
   * @param {Array=} argTypes
   * @param {Array=} args
   * @param {Object=} opts
   */
  var ccall = (ident, returnType, argTypes, args, opts) => {
      // For fast lookup of conversion functions
      var toC = {
        'string': (str) => {
          var ret = 0;
          if (str !== null && str !== undefined && str !== 0) { // null string
            ret = stringToUTF8OnStack(str);
          }
          return ret;
        },
        'array': (arr) => {
          var ret = stackAlloc(arr.length);
          writeArrayToMemory(arr, ret);
          return ret;
        }
      };
  
      function convertReturnValue(ret) {
        if (returnType === 'string') {
          return UTF8ToString(ret);
        }
        if (returnType === 'boolean') return Boolean(ret);
        return ret;
      }
  
      var func = getCFunc(ident);
      var cArgs = [];
      var stack = 0;
      assert(returnType !== 'array', 'return type should not be "array"');
      if (args) {
        for (var i = 0; i < args.length; i++) {
          var converter = toC[argTypes[i]];
          if (converter) {
            if (!stack) stack = stackSave();
            cArgs[i] = converter(args[i]);
          } else {
            cArgs[i] = args[i];
          }
        }
      }
      var ret = func(...cArgs);
      function onDone(ret) {
        if (stack) stackRestore(stack);
        return convertReturnValue(ret);
      }
  
      ret = onDone(ret);
      return ret;
    };

  
    /**
   * @param {string=} returnType
   * @param {Array=} argTypes
   * @param {Object=} opts
   */
  var cwrap = (ident, returnType, argTypes, opts) => {
      return (...args) => ccall(ident, returnType, argTypes, args, opts);
    };

  
  /** @type {!Int16Array} */
  var HEAP16;
  
  
  
  /** @type {!Float32Array} */
  var HEAPF32;
  
  /** @type {!Float64Array} */
  var HEAPF64;
  
  /** not-@type {!BigInt64Array} */
  var HEAP64;
  
    /**
   * @param {number} ptr
   * @param {string} type
   */
  function getValue(ptr, type = 'i8') {
    if (type.endsWith('*')) type = '*';
    switch (type) {
      case 'i1': return HEAP8[ptr];
      case 'i8': return HEAP8[ptr];
      case 'i16': return HEAP16[((ptr)>>1)];
      case 'i32': return HEAP32[((ptr)>>2)];
      case 'i64': return HEAP64[((ptr)>>3)];
      case 'float': return HEAPF32[((ptr)>>2)];
      case 'double': return HEAPF64[((ptr)>>3)];
      case '*': return HEAPU32[((ptr)>>2)];
      default: abort(`invalid type for getValue: ${type}`);
    }
  }

  
  
  
  
  
  
  
    /**
   * @param {number} ptr
   * @param {number} value
   * @param {string} type
   */
  function setValue(ptr, value, type = 'i8') {
    if (type.endsWith('*')) type = '*';
    switch (type) {
      case 'i1': HEAP8[ptr] = value;checkInt8(value); break;
      case 'i8': HEAP8[ptr] = value;checkInt8(value); break;
      case 'i16': HEAP16[((ptr)>>1)] = value;checkInt16(value); break;
      case 'i32': HEAP32[((ptr)>>2)] = value;checkInt32(value); break;
      case 'i64': HEAP64[((ptr)>>3)] = BigInt(value);checkInt64(value); break;
      case 'float': HEAPF32[((ptr)>>2)] = value; break;
      case 'double': HEAPF64[((ptr)>>3)] = value; break;
      case '*': HEAPU32[((ptr)>>2)] = value; break;
      default: abort(`invalid type for setValue: ${type}`);
    }
  }
// End JS library code

// include: postlibrary.js
// This file is included after the automatically-generated JS library code
// but before the wasm module is created.

{

  // Begin ATMODULES hooks
  if (Module['noExitRuntime']) noExitRuntime = Module['noExitRuntime'];
if (Module['print']) out = Module['print'];
if (Module['printErr']) err = Module['printErr'];

Module['FS_createDataFile'] = FS.createDataFile;
Module['FS_createPreloadedFile'] = FS.createPreloadedFile;

  // End ATMODULES hooks

  checkIncomingModuleAPI();

  if (Module['arguments']) programArgs = Module['arguments'];
  if (Module['thisProgram']) thisProgram = Module['thisProgram'];

  // Assertions on removed incoming Module JS APIs.
  assert(typeof Module['memoryInitializerPrefixURL'] == 'undefined', 'Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead');
  assert(typeof Module['pthreadMainPrefixURL'] == 'undefined', 'Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead');
  assert(typeof Module['cdInitializerPrefixURL'] == 'undefined', 'Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead');
  assert(typeof Module['filePackagePrefixURL'] == 'undefined', 'Module.filePackagePrefixURL option was removed, use Module.locateFile instead');
  assert(typeof Module['read'] == 'undefined', 'Module.read option was removed');
  assert(typeof Module['readAsync'] == 'undefined', 'Module.readAsync option was removed (modify readAsync in JS)');
  assert(typeof Module['readBinary'] == 'undefined', 'Module.readBinary option was removed (modify readBinary in JS)');
  assert(typeof Module['setWindowTitle'] == 'undefined', 'Module.setWindowTitle option was removed (modify emscripten_set_window_title in JS)');
  assert(typeof Module['TOTAL_MEMORY'] == 'undefined', 'Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY');
  assert(typeof Module['ENVIRONMENT'] == 'undefined', 'Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)');
  assert(typeof Module['STACK_SIZE'] == 'undefined', 'STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time')
  // If memory is defined in wasm, the user can't provide it, or set INITIAL_MEMORY
  assert(typeof Module['wasmMemory'] == 'undefined', 'Use of `wasmMemory` detected.  Use -sIMPORTED_MEMORY to define wasmMemory externally');
  assert(typeof Module['INITIAL_MEMORY'] == 'undefined', 'Detected runtime INITIAL_MEMORY setting.  Use -sIMPORTED_MEMORY to define wasmMemory dynamically');

  var preInit = Module['preInit'];
  if (preInit) {
    if (typeof preInit == 'function') Module['preInit'] = preInit = [preInit];
    // Written as a loop so that preInit functions that themselves add more
    // preInit functions.  Is this actually needed?
    while (preInit.length > 0) {
      preInit.shift()();
    }
  }
  consumedModuleProp('preInit');
}

// Begin runtime exports
  Module['ccall'] = ccall;
  Module['cwrap'] = cwrap;
  Module['setValue'] = setValue;
  Module['getValue'] = getValue;
  var missingLibrarySymbols = [
  'writeI53ToI64',
  'writeI53ToI64Clamped',
  'writeI53ToI64Signaling',
  'writeI53ToU64Clamped',
  'writeI53ToU64Signaling',
  'readI53FromI64',
  'readI53FromU64',
  'convertI32PairToI53',
  'convertI32PairToI53Checked',
  'convertU32PairToI53',
  'bigintToI53Checked',
  'getTempRet0',
  'setTempRet0',
  'createNamedFunction',
  'zeroMemory',
  'getHeapMax',
  'abortOnCannotGrowMemory',
  'growMemory',
  'withStackSave',
  'strError',
  'inetPton4',
  'inetNtop4',
  'inetPton6',
  'inetNtop6',
  'readSockaddr',
  'writeSockaddr',
  'readEmAsmArgs',
  'jstoi_q',
  'getExecutableName',
  'autoResumeAudioContext',
  'getDynCaller',
  'dynCall',
  'runtimeKeepalivePush',
  'runtimeKeepalivePop',
  'callUserCallback',
  'maybeExit',
  'asyncLoad',
  'asmjsMangle',
  'alignMemory',
  'mmapAlloc',
  'HandleAllocator',
  'getUniqueRunDependency',
  'addRunDependency',
  'removeRunDependency',
  'addOnInit',
  'addOnPostCtor',
  'addOnPreMain',
  'addOnExit',
  'STACK_SIZE',
  'STACK_ALIGN',
  'POINTER_SIZE',
  'ASSERTIONS',
  'convertJsFunctionToWasm',
  'getEmptyTableSlot',
  'updateTableMap',
  'getFunctionAddress',
  'addFunction',
  'removeFunction',
  'intArrayFromString',
  'intArrayToString',
  'AsciiToString',
  'stringToAscii',
  'UTF16ToString',
  'stringToUTF16',
  'lengthBytesUTF16',
  'UTF32ToString',
  'stringToUTF32',
  'lengthBytesUTF32',
  'stringToNewUTF8',
  'registerKeyEventCallback',
  'maybeCStringToJsString',
  'findEventTarget',
  'getBoundingClientRect',
  'fillMouseEventData',
  'registerMouseEventCallback',
  'registerWheelEventCallback',
  'registerUiEventCallback',
  'registerFocusEventCallback',
  'fillDeviceOrientationEventData',
  'registerDeviceOrientationEventCallback',
  'fillDeviceMotionEventData',
  'registerDeviceMotionEventCallback',
  'screenOrientation',
  'fillOrientationChangeEventData',
  'registerOrientationChangeEventCallback',
  'fillFullscreenChangeEventData',
  'registerFullscreenChangeEventCallback',
  'callCanvasResizedCallback',
  'JSEvents_requestFullscreen',
  'JSEvents_resizeCanvasForFullscreen',
  'registerRestoreOldStyle',
  'hideEverythingExceptGivenElement',
  'restoreHiddenElements',
  'setLetterbox',
  'currentFullscreenStrategy',
  'softFullscreenResizeWebGLRenderTarget',
  'doRequestFullscreen',
  'fillPointerlockChangeEventData',
  'registerPointerlockChangeEventCallback',
  'registerPointerlockErrorEventCallback',
  'requestPointerLock',
  'fillVisibilityChangeEventData',
  'registerVisibilityChangeEventCallback',
  'registerTouchEventCallback',
  'fillGamepadEventData',
  'registerGamepadEventCallback',
  'registerBeforeUnloadEventCallback',
  'fillBatteryEventData',
  'registerBatteryEventCallback',
  'setCanvasElementSize',
  'getCanvasElementSize',
  'jsStackTrace',
  'getCallstack',
  'convertPCtoSourceLocation',
  'getEnvStrings',
  'checkWasiClock',
  'flush_NO_FILESYSTEM',
  'wasiRightsToMuslOFlags',
  'wasiOFlagsToMuslOFlags',
  'initRandomFill',
  'randomFill',
  'safeSetTimeout',
  'setImmediateWrapped',
  'safeRequestAnimationFrame',
  'clearImmediateWrapped',
  'registerPostMainLoop',
  'registerPreMainLoop',
  'getPromise',
  'makePromise',
  'addPromise',
  'idsToPromises',
  'makePromiseCallback',
  'Browser_asyncPrepareDataCounter',
  'isLeapYear',
  'ydayFromDate',
  'arraySum',
  'addDays',
  'getSocketFromFD',
  'getSocketAddress',
  'FS_createPreloadedFile',
  'FS_preloadFile',
  'FS_modeStringToFlags',
  'FS_getMode',
  'FS_fileDataToTypedArray',
  'FS_stdin_getChar',
  'FS_mkdirTree',
  '_setNetworkCallback',
  'heapObjectForWebGLType',
  'toTypedArrayIndex',
  'webgl_enable_ANGLE_instanced_arrays',
  'webgl_enable_OES_vertex_array_object',
  'webgl_enable_WEBGL_draw_buffers',
  'webgl_enable_WEBGL_multi_draw',
  'webgl_enable_EXT_polygon_offset_clamp',
  'webgl_enable_EXT_clip_control',
  'webgl_enable_WEBGL_polygon_mode',
  'emscriptenWebGLGet',
  'computeUnpackAlignedImageSize',
  'colorChannelsInGlTextureFormat',
  'emscriptenWebGLGetTexPixelData',
  'emscriptenWebGLGetUniform',
  'webglGetProgramUniformLocation',
  'webglGetUniformLocation',
  'webglPrepareUniformLocationsBeforeFirstUse',
  'webglGetLeftBracePos',
  'emscriptenWebGLGetVertexAttrib',
  '__glGetActiveAttribOrUniform',
  'writeGLArray',
  'registerWebGlEventCallback',
  'runAndAbortIfError',
  'writeStringToMemory',
  'writeAsciiToMemory',
  'allocateUTF8',
  'allocateUTF8OnStack',
  'stackTrace',
  'getNativeTypeSize',
];
missingLibrarySymbols.forEach(missingLibrarySymbol)

  var unexportedSymbols = [
  'run',
  'out',
  'err',
  'callMain',
  'abort',
  'wasmExports',
  'writeStackCookie',
  'checkStackCookie',
  'INT53_MAX',
  'INT53_MIN',
  'HEAP8',
  'HEAPU8',
  'HEAP16',
  'HEAPU16',
  'HEAP32',
  'HEAPU32',
  'HEAPF32',
  'HEAPF64',
  'HEAP64',
  'HEAPU64',
  'stackSave',
  'stackRestore',
  'stackAlloc',
  'ptrToString',
  'exitJS',
  'ENV',
  'setStackLimits',
  'ERRNO_CODES',
  'DNS',
  'Protocols',
  'Sockets',
  'timers',
  'warnOnce',
  'readEmAsmArgsArray',
  'handleException',
  'keepRuntimeAlive',
  'wasmTable',
  'wasmMemory',
  'noExitRuntime',
  'addOnPreRun',
  'addOnPostRun',
  'freeTableIndexes',
  'functionsInTableMap',
  'PATH',
  'PATH_FS',
  'UTF8Decoder',
  'UTF8ArrayToString',
  'UTF8ToString',
  'stringToUTF8Array',
  'stringToUTF8',
  'lengthBytesUTF8',
  'UTF16Decoder',
  'stringToUTF8OnStack',
  'writeArrayToMemory',
  'JSEvents',
  'specialHTMLTargets',
  'findCanvasEventTarget',
  'restoreOldWindowedStyle',
  'UNWIND_CACHE',
  'ExitStatus',
  'emSetImmediate',
  'emClearImmediate_deps',
  'emClearImmediate',
  'promiseMap',
  'Browser',
  'requestFullscreen',
  'setCanvasSize',
  'getUserMedia',
  'createContext',
  'getPreloadedImageData__data',
  'wget',
  'MONTH_DAYS_REGULAR',
  'MONTH_DAYS_LEAP',
  'MONTH_DAYS_REGULAR_CUMULATIVE',
  'MONTH_DAYS_LEAP_CUMULATIVE',
  'SYSCALLS',
  'preloadPlugins',
  'FS_stdin_getChar_buffer',
  'FS_unlink',
  'FS_createPath',
  'FS_createDevice',
  'FS_readFile',
  'FS',
  'FS_root',
  'FS_mounts',
  'FS_devices',
  'FS_streams',
  'FS_nextInode',
  'FS_nameTable',
  'FS_currentPath',
  'FS_initialized',
  'FS_ignorePermissions',
  'FS_filesystems',
  'FS_syncFSRequests',
  'FS_lookupPath',
  'FS_getPath',
  'FS_hashName',
  'FS_hashAddNode',
  'FS_hashRemoveNode',
  'FS_lookupNode',
  'FS_createNode',
  'FS_destroyNode',
  'FS_isRoot',
  'FS_isMountpoint',
  'FS_isFile',
  'FS_isDir',
  'FS_isLink',
  'FS_isChrdev',
  'FS_isBlkdev',
  'FS_isFIFO',
  'FS_isSocket',
  'FS_flagsToPermissionString',
  'FS_nodePermissions',
  'FS_mayLookup',
  'FS_mayCreate',
  'FS_mayDelete',
  'FS_mayOpen',
  'FS_checkOpExists',
  'FS_nextfd',
  'FS_getStreamChecked',
  'FS_getStream',
  'FS_createStream',
  'FS_closeStream',
  'FS_dupStream',
  'FS_doSetAttr',
  'FS_chrdev_stream_ops',
  'FS_major',
  'FS_minor',
  'FS_makedev',
  'FS_registerDevice',
  'FS_getDevice',
  'FS_getMounts',
  'FS_syncfs',
  'FS_mount',
  'FS_unmount',
  'FS_lookup',
  'FS_mknod',
  'FS_statfs',
  'FS_statfsStream',
  'FS_statfsNode',
  'FS_create',
  'FS_mkdir',
  'FS_mkdev',
  'FS_symlink',
  'FS_link',
  'FS_rename',
  'FS_rmdir',
  'FS_readdir',
  'FS_readlink',
  'FS_stat',
  'FS_fstat',
  'FS_lstat',
  'FS_doChmod',
  'FS_chmod',
  'FS_lchmod',
  'FS_fchmod',
  'FS_doChown',
  'FS_chown',
  'FS_lchown',
  'FS_fchown',
  'FS_doTruncate',
  'FS_truncate',
  'FS_ftruncate',
  'FS_utime',
  'FS_open',
  'FS_close',
  'FS_isClosed',
  'FS_llseek',
  'FS_read',
  'FS_write',
  'FS_mmap',
  'FS_msync',
  'FS_ioctl',
  'FS_writeFile',
  'FS_cwd',
  'FS_chdir',
  'FS_createDefaultDirectories',
  'FS_createDefaultDevices',
  'FS_createSpecialDirectories',
  'FS_createStandardStreams',
  'FS_staticInit',
  'FS_init',
  'FS_quit',
  'FS_findObject',
  'FS_analyzePath',
  'FS_createFile',
  'FS_createDataFile',
  'FS_forceLoadFile',
  'FS_createLazyFile',
  'MEMFS',
  'TTY',
  'PIPEFS',
  'SOCKFS',
  'tempFixedLengthArray',
  'miniTempWebGLFloatBuffers',
  'miniTempWebGLIntBuffers',
  'GL',
  'AL',
  'GLUT',
  'EGL',
  'GLEW',
  'IDBStore',
  'SDL',
  'SDL_gfx',
  'print',
  'printErr',
  'jstoi_s',
];
unexportedSymbols.forEach(unexportedRuntimeSymbol);

  // End runtime exports
  // Begin JS library exports
  // End JS library exports

// end include: postlibrary.js

function checkIncomingModuleAPI() {
  ignoredModuleProp('fetchSettings');
  ignoredModuleProp('logReadFiles');
  ignoredModuleProp('loadSplitModule');
  ignoredModuleProp('onMalloc');
  ignoredModuleProp('onRealloc');
  ignoredModuleProp('onFree');
  ignoredModuleProp('onSbrkGrow');
  ignoredModuleProp('onCOSCacheHit');
  ignoredModuleProp('onCOSCacheMiss');
  ignoredModuleProp('onCOSStore');
  ignoredModuleProp('GL_MAX_TEXTURE_IMAGE_UNITS');
  ignoredModuleProp('SDL_canPlayWithWebAudio');
  ignoredModuleProp('SDL_numSimultaneouslyQueuedBuffers');
  ignoredModuleProp('freePreloadedMediaOnUse');
  ignoredModuleProp('preinitializedWebGLContext');
  ignoredModuleProp('keyboardListeningElement');
  ignoredModuleProp('doNotCaptureKeyboard');
  ignoredModuleProp('extraStackTrace');
  ignoredModuleProp('preloadPlugins');
  ignoredModuleProp('preMainLoop');
  ignoredModuleProp('postMainLoop');
  ignoredModuleProp('forcedAspectRatio');
  ignoredModuleProp('mainScriptUrlOrBlob');
  ignoredModuleProp('onFullScreen');
  ignoredModuleProp('INITIAL_MEMORY');
  ignoredModuleProp('wasmMemory');
  ignoredModuleProp('wasmBinary');
}

// Imports from the Wasm binary.
var _init = Module['_init'] = makeInvalidEarlyAccess('_init');
var _update_game = Module['_update_game'] = makeInvalidEarlyAccess('_update_game');
var _render_game = Module['_render_game'] = makeInvalidEarlyAccess('_render_game');
var _get_framebuffer = Module['_get_framebuffer'] = makeInvalidEarlyAccess('_get_framebuffer');
var _get_thirst = Module['_get_thirst'] = makeInvalidEarlyAccess('_get_thirst');
var _get_x = Module['_get_x'] = makeInvalidEarlyAccess('_get_x');
var _get_y = Module['_get_y'] = makeInvalidEarlyAccess('_get_y');
var _get_angle = Module['_get_angle'] = makeInvalidEarlyAccess('_get_angle');
var _get_score = Module['_get_score'] = makeInvalidEarlyAccess('_get_score');
var _get_time = Module['_get_time'] = makeInvalidEarlyAccess('_get_time');
var _get_sound = Module['_get_sound'] = makeInvalidEarlyAccess('_get_sound');
var _get_sprint = Module['_get_sprint'] = makeInvalidEarlyAccess('_get_sprint');
var _get_third_person = Module['_get_third_person'] = makeInvalidEarlyAccess('_get_third_person');
var _toggle_third_person = Module['_toggle_third_person'] = makeInvalidEarlyAccess('_toggle_third_person');
var _get_combo = Module['_get_combo'] = makeInvalidEarlyAccess('_get_combo');
var _get_sandstorm = Module['_get_sandstorm'] = makeInvalidEarlyAccess('_get_sandstorm');
var _set_input = Module['_set_input'] = makeInvalidEarlyAccess('_set_input');
var _main = Module['_main'] = makeInvalidEarlyAccess('_main');
var _fflush = makeInvalidEarlyAccess('_fflush');
var _emscripten_stack_init = makeInvalidEarlyAccess('_emscripten_stack_init');
var _emscripten_stack_get_free = makeInvalidEarlyAccess('_emscripten_stack_get_free');
var _emscripten_stack_get_base = makeInvalidEarlyAccess('_emscripten_stack_get_base');
var _emscripten_stack_get_end = makeInvalidEarlyAccess('_emscripten_stack_get_end');
var __emscripten_stack_restore = makeInvalidEarlyAccess('__emscripten_stack_restore');
var __emscripten_stack_alloc = makeInvalidEarlyAccess('__emscripten_stack_alloc');
var _emscripten_stack_get_current = makeInvalidEarlyAccess('_emscripten_stack_get_current');
var ___set_stack_limits = Module['___set_stack_limits'] = makeInvalidEarlyAccess('___set_stack_limits');
var memory = makeInvalidEarlyAccess('memory');
var __indirect_function_table = makeInvalidEarlyAccess('__indirect_function_table');
var wasmMemory = makeInvalidEarlyAccess('wasmMemory');

function assignWasmExports(wasmExports) {
  assert(typeof wasmExports['init'] != 'undefined', 'missing Wasm export: init');
  assert(typeof wasmExports['update_game'] != 'undefined', 'missing Wasm export: update_game');
  assert(typeof wasmExports['render_game'] != 'undefined', 'missing Wasm export: render_game');
  assert(typeof wasmExports['get_framebuffer'] != 'undefined', 'missing Wasm export: get_framebuffer');
  assert(typeof wasmExports['get_thirst'] != 'undefined', 'missing Wasm export: get_thirst');
  assert(typeof wasmExports['get_x'] != 'undefined', 'missing Wasm export: get_x');
  assert(typeof wasmExports['get_y'] != 'undefined', 'missing Wasm export: get_y');
  assert(typeof wasmExports['get_angle'] != 'undefined', 'missing Wasm export: get_angle');
  assert(typeof wasmExports['get_score'] != 'undefined', 'missing Wasm export: get_score');
  assert(typeof wasmExports['get_time'] != 'undefined', 'missing Wasm export: get_time');
  assert(typeof wasmExports['get_sound'] != 'undefined', 'missing Wasm export: get_sound');
  assert(typeof wasmExports['get_sprint'] != 'undefined', 'missing Wasm export: get_sprint');
  assert(typeof wasmExports['get_third_person'] != 'undefined', 'missing Wasm export: get_third_person');
  assert(typeof wasmExports['toggle_third_person'] != 'undefined', 'missing Wasm export: toggle_third_person');
  assert(typeof wasmExports['get_combo'] != 'undefined', 'missing Wasm export: get_combo');
  assert(typeof wasmExports['get_sandstorm'] != 'undefined', 'missing Wasm export: get_sandstorm');
  assert(typeof wasmExports['set_input'] != 'undefined', 'missing Wasm export: set_input');
  assert(typeof wasmExports['main'] != 'undefined', 'missing Wasm export: main');
  assert(typeof wasmExports['fflush'] != 'undefined', 'missing Wasm export: fflush');
  assert(typeof wasmExports['emscripten_stack_init'] != 'undefined', 'missing Wasm export: emscripten_stack_init');
  assert(typeof wasmExports['emscripten_stack_get_free'] != 'undefined', 'missing Wasm export: emscripten_stack_get_free');
  assert(typeof wasmExports['emscripten_stack_get_base'] != 'undefined', 'missing Wasm export: emscripten_stack_get_base');
  assert(typeof wasmExports['emscripten_stack_get_end'] != 'undefined', 'missing Wasm export: emscripten_stack_get_end');
  assert(typeof wasmExports['_emscripten_stack_restore'] != 'undefined', 'missing Wasm export: _emscripten_stack_restore');
  assert(typeof wasmExports['_emscripten_stack_alloc'] != 'undefined', 'missing Wasm export: _emscripten_stack_alloc');
  assert(typeof wasmExports['emscripten_stack_get_current'] != 'undefined', 'missing Wasm export: emscripten_stack_get_current');
  assert(typeof wasmExports['__set_stack_limits'] != 'undefined', 'missing Wasm export: __set_stack_limits');
  assert(typeof wasmExports['memory'] != 'undefined', 'missing Wasm export: memory');
  assert(typeof wasmExports['__indirect_function_table'] != 'undefined', 'missing Wasm export: __indirect_function_table');
  _init = Module['_init'] = createExportWrapper('init', wasmExports['init'], 0);
  _update_game = Module['_update_game'] = createExportWrapper('update_game', wasmExports['update_game'], 1);
  _render_game = Module['_render_game'] = createExportWrapper('render_game', wasmExports['render_game'], 0);
  _get_framebuffer = Module['_get_framebuffer'] = createExportWrapper('get_framebuffer', wasmExports['get_framebuffer'], 0);
  _get_thirst = Module['_get_thirst'] = createExportWrapper('get_thirst', wasmExports['get_thirst'], 0);
  _get_x = Module['_get_x'] = createExportWrapper('get_x', wasmExports['get_x'], 0);
  _get_y = Module['_get_y'] = createExportWrapper('get_y', wasmExports['get_y'], 0);
  _get_angle = Module['_get_angle'] = createExportWrapper('get_angle', wasmExports['get_angle'], 0);
  _get_score = Module['_get_score'] = createExportWrapper('get_score', wasmExports['get_score'], 0);
  _get_time = Module['_get_time'] = createExportWrapper('get_time', wasmExports['get_time'], 0);
  _get_sound = Module['_get_sound'] = createExportWrapper('get_sound', wasmExports['get_sound'], 0);
  _get_sprint = Module['_get_sprint'] = createExportWrapper('get_sprint', wasmExports['get_sprint'], 0);
  _get_third_person = Module['_get_third_person'] = createExportWrapper('get_third_person', wasmExports['get_third_person'], 0);
  _toggle_third_person = Module['_toggle_third_person'] = createExportWrapper('toggle_third_person', wasmExports['toggle_third_person'], 0);
  _get_combo = Module['_get_combo'] = createExportWrapper('get_combo', wasmExports['get_combo'], 0);
  _get_sandstorm = Module['_get_sandstorm'] = createExportWrapper('get_sandstorm', wasmExports['get_sandstorm'], 0);
  _set_input = Module['_set_input'] = createExportWrapper('set_input', wasmExports['set_input'], 3);
  _main = Module['_main'] = createExportWrapper('main', wasmExports['main'], 2);
  _fflush = createExportWrapper('fflush', wasmExports['fflush'], 1);
  _emscripten_stack_init = wasmExports['emscripten_stack_init'];
  _emscripten_stack_get_free = wasmExports['emscripten_stack_get_free'];
  _emscripten_stack_get_base = wasmExports['emscripten_stack_get_base'];
  _emscripten_stack_get_end = wasmExports['emscripten_stack_get_end'];
  __emscripten_stack_restore = wasmExports['_emscripten_stack_restore'];
  __emscripten_stack_alloc = wasmExports['_emscripten_stack_alloc'];
  _emscripten_stack_get_current = wasmExports['emscripten_stack_get_current'];
  ___set_stack_limits = Module['___set_stack_limits'] = createExportWrapper('__set_stack_limits', wasmExports['__set_stack_limits'], 2);
  memory = wasmMemory = wasmExports['memory'];
  __indirect_function_table = wasmExports['__indirect_function_table'];
}

var wasmImports = {
  /** @export */
  __handle_stack_overflow: ___handle_stack_overflow
};


// include: postamble.js
// === Auto-generated postamble setup entry stuff ===

var calledRun;

function callMain() {
  assert(typeof onPreRuns === 'undefined' || onPreRuns.length == 0, 'cannot call main when preRun functions remain to be called');

  var entryFunction = _main;

  var argc = 0;
  var argv = 0;

  try {

    var ret = entryFunction(argc, argv);

    // if we're not running an evented main loop, it's time to exit
    exitJS(ret, /* implicit = */ true);
    return ret;
  } catch (e) {
    return handleException(e);
  }
}

function stackCheckInit() {
  // This is normally called automatically during __wasm_call_ctors but need to
  // get these values before even running any of the ctors so we call it redundantly
  // here.
  _emscripten_stack_init();
  // TODO(sbc): Move writeStackCookie to native to to avoid this.
  writeStackCookie();
}

async function run() {
  assert(!calledRun);
  calledRun = true;

  stackCheckInit();

  preRun();

  var setStatus = Module['setStatus'];
  if (setStatus) {
    setStatus('Running...');
    // Yield to the event loop to allow the browser to paint "Running..."
    await new Promise((resolve) => setTimeout(resolve, 1));
    // Then we want to clear the status text, but only after the rest of this function runs.
    setTimeout(setStatus, 1, '');
  }

  if (ABORT) return;

  initRuntime();

  // No ATMAINS hooks

  Module['onRuntimeInitialized']?.();
  consumedModuleProp('onRuntimeInitialized');

  var noInitialRun = Module['noInitialRun'] || false;
  if (!noInitialRun) callMain();

  postRun();
}

function checkUnflushedContent() {
  // Compiler settings do not allow exiting the runtime, so flushing
  // the streams is not possible. but in ASSERTIONS mode we check
  // if there was something to flush, and if so tell the user they
  // should request that the runtime be exitable.
  // Normally we would not even include flush() at all, but in ASSERTIONS
  // builds we do so just for this check, and here we see if there is any
  // content to flush, that is, we check if there would have been
  // something a non-ASSERTIONS build would have not seen.
  // How we flush the streams depends on whether we are in SYSCALLS_REQUIRE_FILESYSTEM=0
  // mode (which has its own special function for this; otherwise, all
  // the code is inside libc)
  var oldOut = out;
  var oldErr = err;
  var has = false;
  out = err = (x) => {
    has = true;
  }
  try { // it doesn't matter if it fails
    _fflush(0);
  } catch(e) {}
  out = oldOut;
  err = oldErr;
  if (has) {
    warnOnce('stdio streams had content in them that was not flushed. you should set EXIT_RUNTIME to 1 (see the Emscripten FAQ), or make sure to emit a newline when you printf etc.');
    warnOnce('(this may also be due to not including full filesystem support - try building with -sFORCE_FILESYSTEM)');
  }
}

var wasmExports;

// With async instantation wasmExports is assigned asynchronously when the
// instance is received.
createWasm().then(() => run());

// end include: postamble.js

