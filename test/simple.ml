(* Test for simple, common objects. *)

type t0 = A | B | C | D | E

type t1 = { a: t1; b : t0; c: t0 }

type t2 = { x: t1; y: t1 }

let check msg b = Alcotest.(check bool msg true b)

let test_cycle () =
  (* Tests a cycle. *)
  let rec x = { a = x; b = B; c = C } in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z.a == z);
  check "" (z.b = B);
  check "" (z.c = C);
  Offheap.delete y

let test_bytes () =
  (* Tests a sequence of bytes. *)
  let x =
      "I know a mouse, and he hasn’t got a house" ^
      "I don't know why. I call him Gerald." ^
      "He's getting rather old, but he's a good mouse."
  in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z = x);
  Offheap.delete y

let test_complex () =
  (* Tests a complex object. *)
  let rec a = { a = a; b = B; c = C } in
  let rec b = { a = b; b = C; c = B } in
  let x = { x = a; y = b } in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z.x.a == z.x);
  check "" (z.y.a == z.y);
  check "" (z.x.b = B && z.x.c = C);
  check "" (z.y.b = C && z.y.c = B);
  Offheap.delete y

let test_closure () =
  (* Tests a closure. *)
  let n = "1" in
  let x = Some (fun () -> int_of_string n + 2) in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  let n = match z with None -> 1 | Some f -> f () in
  check "" (n = 3);
  Offheap.delete y

let test_abstract () =
  (* Should fail if object is abstract. *)
  try
    let rec x = { a = x; b = B; c = C } in
    ignore (Offheap.copy (Offheap.copy x));
    failwith "failed"
  with Invalid_argument _ -> ()

let test_primitives () =
  (* Should handle primitives. *)
  let x = 1 in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z = x);
  Offheap.delete y

let test_static () =
  (* Should handle static data. *)
  let x = "123" in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z = x);
  Offheap.delete y

let test_nativeint () =
  (* Should handle native ints. *)
  let x = Nativeint.one in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (z = x);
  Offheap.delete y

let test_page_table_add () =
  (* Should be marked as being part of a value area so compare/hash work. *)
  let x = Some (
    "I've got a clan of gingerbread men." ^
    "Here a man, there a man, lots of gingerbread men." ^
    "Take a couple if you wish. They're on the dish."
  ) in
  let y = Offheap.copy x in
  let z = Offheap.get y in
  check "" (Stdlib.compare x z = 0);
  check "" (Hashtbl.hash x = Hashtbl.hash z);
  Offheap.delete y

let tests = [
  "test_cycle", `Quick, test_cycle;
  "test_bytes", `Quick, test_bytes;
  "test_complex", `Quick, test_complex;
  "test_closure", `Quick, test_closure;
  "test_abstract", `Quick, test_abstract;
  "test_primitives", `Quick, test_primitives;
  "test_static", `Quick, test_static;
  "test_nativeint", `Quick, test_nativeint;
  "test_page_table_add", `Quick, test_page_table_add;
]