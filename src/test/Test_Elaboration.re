open Alcotest;
open Haz3lcore;
open DExp;

let dhexp_eq = (d1: option(DExp.t), d2: option(DExp.t)): bool =>
  switch (d1, d2) {
  | (Some(d1), Some(d2)) => DExp.fast_equal(d1, d2)
  | _ => false
  };

let dhexp_print = (d: option(DExp.t)): string =>
  switch (d) {
  | None => "None"
  | Some(d) => DExp.show(d)
  };

/*Create a testable type for dhexp which requires
  an equal function (dhexp_eq) and a print function (dhexp_print) */
let dhexp_typ = testable(Fmt.using(dhexp_print, Fmt.string), dhexp_eq);

let ids = List.init(12, _ => Id.mk());
let id_at = x => x |> List.nth(ids);
let mk_map = CoreSettings.on |> Interface.Statics.mk_map;
let dhexp_of_uexp = u => Elaborator.dhexp_of_uexp(mk_map(u), u, false);
let alco_check = dhexp_typ |> Alcotest.check;

let u1: UExp.t = {ids: [id_at(0)], copied: false, term: Int(8)};
let single_integer = () =>
  alco_check(
    "Integer literal 8",
    Some(Int(8) |> fresh),
    dhexp_of_uexp(u1),
  );

let u2: UExp.t = {ids: [id_at(0)], copied: false, term: EmptyHole};
let empty_hole = () =>
  alco_check("Empty hole", Some(EmptyHole |> fresh), dhexp_of_uexp(u2));

let u3: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term: Parens({ids: [id_at(1)], copied: false, term: Var("y")}),
};
let d3: DExp.t = StaticErrorHole(id_at(1), Var("y") |> fresh) |> fresh;
let free_var = () =>
  alco_check(
    "Nonempty hole with free variable",
    Some(d3),
    dhexp_of_uexp(u3),
  );

let u4: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    Let(
      {
        ids: [id_at(1)],
        term:
          Tuple([
            {ids: [id_at(2)], term: Var("a")},
            {ids: [id_at(3)], term: Var("b")},
          ]),
      },
      {
        ids: [id_at(4)],
        copied: false,
        term:
          Tuple([
            {ids: [id_at(5)], copied: false, term: Int(4)},
            {ids: [id_at(6)], copied: false, term: Int(6)},
          ]),
      },
      {
        ids: [id_at(7)],
        copied: false,
        term:
          BinOp(
            Int(Minus),
            {ids: [id_at(8)], copied: false, term: Var("a")},
            {ids: [id_at(9)], copied: false, term: Var("b")},
          ),
      },
    ),
};
let d4: DExp.t =
  Let(
    Tuple([Var("a") |> DHPat.fresh, Var("b") |> DHPat.fresh]) |> DHPat.fresh,
    Tuple([Int(4) |> fresh, Int(6) |> fresh]) |> fresh,
    BinOp(Int(Minus), Var("a") |> fresh, Var("b") |> fresh) |> fresh,
  )
  |> fresh;
let let_exp = () =>
  alco_check(
    "Let expression for tuple (a, b)",
    Some(d4),
    dhexp_of_uexp(u4),
  );

let u5: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    BinOp(
      Int(Plus),
      {ids: [id_at(1)], copied: false, term: Bool(false)},
      {ids: [id_at(2)], copied: false, term: Var("y")},
    ),
};
let d5: DExp.t =
  BinOp(
    Int(Plus),
    StaticErrorHole(id_at(1), Bool(false) |> fresh) |> fresh,
    StaticErrorHole(id_at(2), Var("y") |> fresh) |> fresh,
  )
  |> fresh;
let bin_op = () =>
  alco_check(
    "Inconsistent binary integer operation (plus)",
    Some(d5),
    dhexp_of_uexp(u5),
  );

let u6: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    If(
      {ids: [id_at(1)], copied: false, term: Bool(false)},
      {ids: [id_at(2)], copied: false, term: Int(8)},
      {ids: [id_at(3)], copied: false, term: Int(6)},
    ),
};
let d6: DExp.t =
  If(Bool(false) |> fresh, Int(8) |> fresh, Int(6) |> fresh) |> fresh;
let consistent_if = () =>
  alco_check(
    "Consistent case with rules (BoolLit(true), IntLit(8)) and (BoolLit(false), IntLit(6))",
    Some(d6),
    dhexp_of_uexp(u6),
  );

let u7: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    Ap(
      Forward,
      {
        ids: [id_at(1)],
        copied: false,
        term:
          Fun(
            {ids: [id_at(2)], term: Var("x")},
            {
              ids: [id_at(3)],
              copied: false,
              term:
                BinOp(
                  Int(Plus),
                  {ids: [id_at(4)], copied: false, term: Int(4)},
                  {ids: [id_at(5)], copied: false, term: Var("x")},
                ),
            },
            None,
            None,
          ),
      },
      {ids: [id_at(6)], copied: false, term: Var("y")},
    ),
};
let d7: DExp.t =
  Ap(
    Forward,
    Fun(
      Var("x") |> DHPat.fresh,
      BinOp(
        Int(Plus),
        Int(4) |> fresh,
        Cast(Var("x") |> fresh, Unknown(Internal), Int) |> fresh,
      )
      |> fresh,
      None,
      None,
    )
    |> fresh,
    StaticErrorHole(id_at(6), Var("y") |> fresh) |> fresh,
  )
  |> fresh;
let ap_fun = () =>
  alco_check(
    "Application of a function of a free variable wrapped inside a nonempty hole constructor",
    Some(d7),
    dhexp_of_uexp(u7),
  );

let u8: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    Match(
      {
        ids: [id_at(1)],
        copied: false,
        term:
          BinOp(
            Int(Equals),
            {ids: [id_at(2)], copied: false, term: Int(4)},
            {ids: [id_at(3)], copied: false, term: Int(3)},
          ),
      },
      [
        (
          {ids: [id_at(6)], term: Bool(true)},
          {ids: [id_at(4)], copied: false, term: Int(24)},
        ),
        (
          {ids: [id_at(7)], term: Bool(false)},
          {ids: [id_at(5)], copied: false, term: Bool(false)},
        ),
      ],
    ),
};
let d8scrut: DExp.t =
  BinOp(Int(Equals), Int(4) |> fresh, Int(3) |> fresh) |> fresh;
let d8rules =
  DExp.[
    (Bool(true) |> DHPat.fresh, Int(24) |> fresh),
    (Bool(false) |> DHPat.fresh, Bool(false) |> fresh),
  ];
let d8a: DExp.t = Match(d8scrut, d8rules) |> fresh;
let d8: DExp.t = StaticErrorHole(id_at(0), d8a) |> fresh;
let inconsistent_case = () =>
  alco_check(
    "Inconsistent branches where the first branch is an integer and second branch is a boolean",
    Some(d8),
    dhexp_of_uexp(u8),
  );

let u9: UExp.t = {
  ids: [id_at(0)],
  copied: false,
  term:
    Let(
      {
        ids: [id_at(1)],
        term:
          TypeAnn(
            {ids: [id_at(2)], term: Var("f")},
            {
              ids: [id_at(3)],
              term:
                Arrow(
                  {ids: [id_at(4)], term: Int},
                  {ids: [id_at(5)], term: Int},
                ),
            },
          ),
      },
      {
        ids: [id_at(6)],
        copied: false,
        term:
          Fun(
            {ids: [id_at(7)], term: Var("x")},
            {
              ids: [id_at(8)],
              copied: false,
              term:
                BinOp(
                  Int(Plus),
                  {ids: [id_at(9)], copied: false, term: Int(1)},
                  {ids: [id_at(10)], copied: false, term: Var("x")},
                ),
            },
            None,
            None,
          ),
      },
      {ids: [id_at(11)], copied: false, term: Int(55)},
    ),
};
// let d9: DExp.t =
//   Let(
//     Var("f"),
//     FixF(
//       "f",
//       Arrow(Int, Int),
//       Fun(
//         Var("x"),
//         Int,
//         BinOp(Int(Plus), Int(1) |> fresh, Var("x") |> fresh) |> fresh,
//         None,
//         Some("f"),
//       )
//       |> fresh,
//     )
//     |> fresh,
//     Int(55) |> fresh,
//   )
//   |> fresh;
// let let_fun = () =>
//   alco_check(
//     "Let expression for function which wraps a fix point constructor around the function",
//     Some(d9),
//     dhexp_of_uexp(u9),
//   );

let elaboration_tests = [
  test_case("Single integer", `Quick, single_integer),
  test_case("Empty hole", `Quick, empty_hole),
  test_case("Free variable", `Quick, free_var),
  test_case("Let expression", `Quick, let_exp),
  test_case("Inconsistent binary operation", `Quick, bin_op),
  test_case("Consistent if statement", `Quick, consistent_if),
  test_case("Application of function on free variable", `Quick, ap_fun),
  test_case("Inconsistent case statement", `Quick, inconsistent_case),
  // test_case("Let expression for a function", `Quick, let_fun),
];
