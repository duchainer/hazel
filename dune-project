(lang dune 3.16)
(using menhir 2.0)

(name hazel)

(generate_opam_files true)

(source
 (github hazelgrove/hazel))

(authors "Hazel Development Team")
(maintainers "Hazel Development Team")

(license MIT)

(package
 (name hazel)
 (allow_empty)
 (synopsis "Hazel, a live functional programming environment with typed holes")
;  (description "A longer description")
 ; (tags
 ; (topics "to describe" your project))
 (depends
  (ocaml
   (>= 5.2.0))
  (menhir
   (>= 2.0))
  yojson
  reason
  ppx_yojson_conv_lib
  ppx_yojson_conv
  incr_dom
  (omd (>= 2.0.0~alpha4))
  ezjs_idb
  virtual_dom
  ppx_deriving
  ptmap
  uuidm
  unionFind
  ocamlformat
  (junit_alcotest :with-test)
  ocaml-lsp-server)) ; After upgrading to opam 2.2 use with-dev https://opam.ocaml.org/blog/opam-2-2-0/

; See the complete stanza docs at https://dune.readthedocs.io/en/stable/reference/dune-project/index.html
