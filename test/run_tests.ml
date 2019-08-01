(*
   Run all the OCaml test suites defined in the project.
*)

let test_suites: unit Alcotest.test list = [
  "Simple tests", Simple.tests;
]

let () = Alcotest.run "proj" test_suites
