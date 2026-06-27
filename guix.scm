;;; enet/guix.scm --- Guix package for the enet C++20 networking library.
;;;
;;; Two-file build: this guix.scm + the repo Makefile.  enet is a compiled
;;; library that installs a static archive (libenet.a) plus its headers.  It
;;; depends on the exstd sibling (header-only, for singleton.hpp etc.) and on a
;;; handful of third-party libraries pulled from Guix:
;;;
;;;   openssl / zlib       -- TLS + compression for the http/https stack
;;;   libmd                -- <md5.h> used by the DHT implementation
;;;   util-linux           -- libuuid (<uuid/uuid.h>)
;;;   i2pd                 -- OPTIONAL I2P support (see below)
;;;
;;; De-vendored: the entire vendors/ tree is gone.  exstd's headers come from
;;; the sibling guix.scm; everything else comes from Guix inputs on the
;;; compiler's CPATH / LIBRARY_PATH search paths.
;;;
;;; I2P support is OPTIONAL.  Upstream i2pd is built by Guix as a daemon and
;;; does not export a consumable libi2pd.a + header tree, so the core library
;;; (libenet.a = the DHT object) builds WITHOUT i2p by default.  The Makefile
;;; gates the i2p translation unit behind `I2P=1`; flip the make-flag and the
;;; i2pd input is on the search path when an i2pd that exports the library and
;;; headers is available.
;;;
;;; Built two ways from the same tree:
;;;   CI:    guix build -f guix.scm
;;;   Local: make inside `guix shell -D -f guix.scm`

(use-modules (guix packages)
             (guix gexp)
             (guix git)
             (guix git-download)
             (guix build-system gnu)
             (guix build-system copy)
             ((guix licenses) #:prefix license:))

(load "/home/dots/Documents/Projects/frameworks/guix/lib.scm")

;; Sibling e* dependency: exstd (header-only).  Referenced by a path relative to
;; this guix.scm so the flat frameworks/<repo> layout resolves siblings.
(define exstd
  (load (string-append (dirname (current-filename)) "/../exstd/guix.scm")))

(package
  (name "enet")
  (version "0.1.0")
  (source (repo-source (dirname (current-filename)) "enet-src"))
  (build-system gnu-build-system)
  (arguments
   (list
    #:tests? #f
    #:make-flags
    #~(list (string-append "PREFIX=" #$output)
            "CXX=g++"
            "CC=gcc"
            "AR=gcc-ar")
    #:phases
    #~(modify-phases %standard-phases
        (delete 'configure))))
  ;; All deps are statically linked, so none are propagated.  Consumers that
  ;; include enet's public headers (which pull <openssl/*>, <md5.h>, exstd
  ;; zstream -> zlib) list these same static deps among their own inputs and
  ;; link them statically.  i2pd is a REQUIRED dependency (I2P transport is a
  ;; first-class feature, not optional).
  ;; exstd sibling + enet's full third-party closure (%enet-3p: openssl, libmd,
  ;; util-linux, i2pd-lib, boost-1.83, zlib).  None propagated; all linked
  ;; statically.  i2pd-lib provides libi2pd*.a + headers (I2P is first-class).
  (inputs (cons exstd %enet-3p))
  (native-inputs (list gcc-toolchain %pkg-config))
  (synopsis "C++20 networking library (HTTP, HTTPS, DHT, optional I2P)")
  (description
   "enet is a C++20 networking library providing TCP/UDP helpers, an HTTP and
HTTPS client/server stack (OpenSSL), a Kademlia-style DHT, a network buffer
abstraction, and optional I2P transport.  It builds to a static archive
(libenet.a) consumed by downstream framework repositories such as erpc and
eengine.  I2P support is optional and gated behind the i2pd input.")
  (home-page "https://github.com/RealAstolfo/enet")
  (license license:expat))
