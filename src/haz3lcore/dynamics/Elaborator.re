open Util;
open OptUtil.Syntax;

module Elaboration = {
  [@deriving (show({with_path: false}), sexp, yojson)]
  type t = {
    d: DHExp.t,
    info_map: Statics.Map.t,
  };
};

module ElaborationResult = {
  [@deriving sexp]
  type t =
    | Elaborates(DHExp.t, Typ.t, Delta.t)
    | DoesNotElaborate;
};

let fixed_exp_typ = (m: Statics.Map.t, e: Term.UExp.t): option(Typ.t) =>
  switch (Id.Map.find_opt(Term.UExp.rep_id(e), m)) {
  | Some(InfoExp({ty, _})) => Some(ty)
  | _ => None
  };

let fixed_pat_typ = (m: Statics.Map.t, p: Term.UPat.t): option(Typ.t) =>
  switch (Id.Map.find_opt(Term.UPat.rep_id(p), m)) {
  | Some(InfoPat({ty, _})) => Some(ty)
  | _ => None
  };

let cast = (ctx: Ctx.t, mode: Mode.t, self_ty: Typ.t, d: DHExp.t) =>
  switch (mode) {
  | Syn => d
  | SynFun =>
    switch (self_ty) {
    | Unknown(prov) =>
      DHExp.fresh_cast(
        d,
        Unknown(prov),
        Arrow(Unknown(prov), Unknown(prov)),
      )
    | Arrow(_) => d
    | _ => failwith("Elaborator.wrap: SynFun non-arrow-type")
    }
  | Ana(ana_ty) =>
    let ana_ty = Typ.normalize(ctx, ana_ty);
    /* Forms with special ana rules get cast from their appropriate Matched types */
    switch (DHExp.term_of(d)) {
    | ListLit(_)
    | ListConcat(_)
    | Cons(_) =>
      switch (ana_ty) {
      | Unknown(prov) =>
        DHExp.fresh_cast(d, List(Unknown(prov)), Unknown(prov))
      | _ => d
      }
    | Fun(_) =>
      /* See regression tests in Documentation/Dynamics */
      let (_, ana_out) = Typ.matched_arrow(ctx, ana_ty);
      let (self_in, _) = Typ.matched_arrow(ctx, self_ty);
      DHExp.fresh_cast(d, Arrow(self_in, ana_out), ana_ty);
    | Tuple(ds) =>
      switch (ana_ty) {
      | Unknown(prov) =>
        let us = List.init(List.length(ds), _ => Typ.Unknown(prov));
        DHExp.fresh_cast(d, Prod(us), Unknown(prov));
      | _ => d
      }
    | Constructor(_) =>
      switch (ana_ty, self_ty) {
      | (Unknown(prov), Rec(_, Sum(_)))
      | (Unknown(prov), Sum(_)) =>
        DHExp.fresh_cast(d, self_ty, Unknown(prov))
      | _ => d
      }
    | Ap(_, f, _) =>
      switch (DHExp.term_of(f)) {
      | Constructor(_) =>
        switch (ana_ty, self_ty) {
        | (Unknown(prov), Rec(_, Sum(_)))
        | (Unknown(prov), Sum(_)) =>
          DHExp.fresh_cast(d, self_ty, Unknown(prov))
        | _ => d
        }
      | NonEmptyHole(_, _, _, g) =>
        switch (DHExp.term_of(g)) {
        | Constructor(_) =>
          switch (ana_ty, self_ty) {
          | (Unknown(prov), Rec(_, Sum(_)))
          | (Unknown(prov), Sum(_)) =>
            DHExp.fresh_cast(d, self_ty, Unknown(prov))
          | _ => d
          }
        | _ => DHExp.fresh_cast(d, self_ty, ana_ty)
        }
      | _ => DHExp.fresh_cast(d, self_ty, ana_ty)
      }
    /* Forms with special ana rules but no particular typing requirements */
    | Match(_)
    | If(_)
    | Seq(_)
    | Let(_)
    | FixF(_) => d
    /* Hole-like forms: Don't cast */
    | InvalidText(_)
    | FreeVar(_)
    | EmptyHole
    | NonEmptyHole(_) => d
    /* DHExp-specific forms: Don't cast */
    | Cast(_)
    | Closure(_)
    | Filter(_)
    | FailedCast(_)
    | InvalidOperation(_) => d
    /* Normal cases: wrap */
    | Var(_)
    | ApBuiltin(_)
    | BuiltinFun(_)
    | Prj(_)
    | Bool(_)
    | Int(_)
    | Float(_)
    | String(_)
    | BinOp(_)
    | Test(_) => DHExp.fresh_cast(d, self_ty, ana_ty)
    };
  };

/* Handles cast insertion and non-empty-hole wrapping
   for elaborated expressions */
let wrap = (ctx: Ctx.t, u: Id.t, mode: Mode.t, self, d: DHExp.t): DHExp.t =>
  switch (Info.status_exp(ctx, mode, self)) {
  | NotInHole(_) =>
    let self_ty =
      switch (Self.typ_of_exp(ctx, self)) {
      | Some(self_ty) => Typ.normalize(ctx, self_ty)
      | None => Unknown(Internal)
      };
    cast(ctx, mode, self_ty, d);
  | InHole(_) => DHExp.fresh(NonEmptyHole(TypeInconsistent, u, 0, d))
  };

let rec dhexp_of_uexp =
        (m: Statics.Map.t, uexp: Term.UExp.t, in_filter: bool)
        : option(DHExp.t) => {
  let dhexp_of_uexp = (~in_filter=in_filter, m, uexp) => {
    dhexp_of_uexp(m, uexp, in_filter);
  };
  switch (Id.Map.find_opt(Term.UExp.rep_id(uexp), m)) {
  | Some(InfoExp({mode, self, ctx, _})) =>
    let err_status = Info.status_exp(ctx, mode, self);
    let id = Term.UExp.rep_id(uexp); /* NOTE: using term uids for hole ids */
    let rewrap = DHExp.mk(uexp.ids);
    let+ d: DHExp.t =
      switch (uexp.term) {
      | Invalid(t) => Some(DHExp.InvalidText(id, 0, t) |> rewrap)
      | EmptyHole => Some(DHExp.EmptyHole |> rewrap)
      | MultiHole(_tms) =>
        /* TODO: add a dhexp case and eval logic for multiholes.
           Make sure new dhexp form is properly considered Indet
           to avoid casting issues. */
        Some(EmptyHole |> rewrap)
      | Triv => Some(Tuple([]) |> rewrap)
      | Bool(b) => Some(Bool(b) |> rewrap)
      | Int(n) => Some(Int(n) |> rewrap)
      | Float(n) => Some(Float(n) |> rewrap)
      | String(s) => Some(String(s) |> rewrap)
      | ListLit(es) =>
        let* ds = es |> List.map(dhexp_of_uexp(m)) |> OptUtil.sequence;
        let+ ty = fixed_exp_typ(m, uexp);
        let ty = Typ.matched_list(ctx, ty);
        DHExp.ListLit(id, 0, ty, ds) |> rewrap;
      | Fun(p, body) =>
        let* dp = dhpat_of_upat(m, p);
        let* d1 = dhexp_of_uexp(m, body);
        let+ ty = fixed_pat_typ(m, p);
        DHExp.Fun(dp, ty, d1, None, None) |> rewrap;
      | Tuple(es) =>
        let+ ds = es |> List.map(dhexp_of_uexp(m)) |> OptUtil.sequence;
        DHExp.Tuple(ds) |> rewrap;
      | Cons(e1, e2) =>
        let* dc1 = dhexp_of_uexp(m, e1);
        let+ dc2 = dhexp_of_uexp(m, e2);
        DHExp.Cons(dc1, dc2) |> rewrap;
      | ListConcat(e1, e2) =>
        let* dc1 = dhexp_of_uexp(m, e1);
        let+ dc2 = dhexp_of_uexp(m, e2);
        DHExp.ListConcat(dc1, dc2) |> rewrap;
      | UnOp(Meta(Unquote), e) =>
        switch (e.term) {
        | Var("e") when in_filter => Some(Constructor("$e") |> DHExp.fresh)
        | Var("v") when in_filter => Some(Constructor("$v") |> DHExp.fresh)
        | _ => Some(DHExp.EmptyHole |> rewrap)
        }
      | UnOp(Int(Minus), e) =>
        let+ dc = dhexp_of_uexp(m, e);
        DHExp.BinOp(Int(Minus), DHExp.fresh(Int(0)), dc) |> rewrap;
      | UnOp(Bool(Not), e) =>
        let+ d_scrut = dhexp_of_uexp(m, e);
        let d_rules = [
          (DHPat.Bool(true), DHExp.(fresh(Bool(false)))),
          (DHPat.Bool(false), DHExp.(fresh(Bool(true)))),
        ];
        let d = DHExp.(fresh(Match(Consistent, d_scrut, d_rules)));
        /* Manually construct cast (case is not otherwise cast) */
        switch (mode) {
        | Ana(ana_ty) => DHExp.fresh_cast(d, Bool, ana_ty)
        | _ => d
        };
      | BinOp(op, e1, e2) =>
        let* dc1 = dhexp_of_uexp(m, e1);
        let+ dc2 = dhexp_of_uexp(m, e2);
        DHExp.BinOp(op, dc1, dc2) |> rewrap;
      | Parens(e) => dhexp_of_uexp(m, e)
      | Seq(e1, e2) =>
        let* d1 = dhexp_of_uexp(m, e1);
        let+ d2 = dhexp_of_uexp(m, e2);
        DHExp.Seq(d1, d2) |> rewrap;
      | Test(test) =>
        let+ dtest = dhexp_of_uexp(m, test);
        DHExp.Test(id, dtest) |> rewrap;
      | Filter(act, cond, body) =>
        let* dcond = dhexp_of_uexp(~in_filter=true, m, cond);
        let+ dbody = dhexp_of_uexp(m, body);
        DHExp.Filter(Filter(Filter.mk(dcond, act)), dbody) |> rewrap;
      | Var(name) =>
        switch (err_status) {
        | InHole(FreeVariable(_)) => Some(FreeVar(id, 0, name) |> rewrap)
        | _ => Some(Var(name) |> rewrap)
        }
      | Constructor(name) =>
        switch (err_status) {
        | InHole(Common(NoType(FreeConstructor(_)))) =>
          Some(FreeVar(id, 0, name) |> rewrap)
        | _ => Some(Constructor(name) |> rewrap)
        }
      | Let(p, def, body) =>
        let add_name: (option(string), DHExp.t) => DHExp.t = (
          (name, d) => {
            let (term, rewrap) = DHExp.unwrap(d);
            switch (term) {
            | Fun(p, ty, e, ctx, _) =>
              DHExp.Fun(p, ty, e, ctx, name) |> rewrap
            | _ => d
            };
          }
        );
        let* dp = dhpat_of_upat(m, p);
        let* ddef = dhexp_of_uexp(m, def);
        let* dbody = dhexp_of_uexp(m, body);
        let+ ty = fixed_pat_typ(m, p);
        switch (Term.UPat.get_recursive_bindings(p)) {
        | None =>
          /* not recursive */
          DHExp.Let(dp, add_name(Term.UPat.get_var(p), ddef), dbody)
          |> rewrap
        | Some([f]) =>
          /* simple recursion */
          Let(
            dp,
            FixF(f, ty, add_name(Some(f), ddef)) |> DHExp.fresh,
            dbody,
          )
          |> rewrap
        | Some(fs) =>
          /* mutual recursion */
          let ddef =
            switch (DHExp.term_of(ddef)) {
            | Tuple(a) =>
              DHExp.Tuple(List.map2(s => add_name(Some(s)), fs, a))
              |> DHExp.fresh
            | _ => ddef
            };
          let uniq_id = List.nth(def.ids, 0);
          let self_id = "__mutual__" ++ Id.to_string(uniq_id);
          // TODO: Re-use IDs here instead of using fresh
          let self_var = DHExp.Var(self_id) |> DHExp.fresh;
          let (_, substituted_def) =
            fs
            |> List.fold_left(
                 ((i, ddef), f) => {
                   let ddef =
                     Substitution.subst_var(
                       DHExp.Prj(self_var, i) |> DHExp.fresh,
                       f,
                       ddef,
                     );
                   (i + 1, ddef);
                 },
                 (0, ddef),
               );
          Let(dp, FixF(self_id, ty, substituted_def) |> DHExp.fresh, dbody)
          |> rewrap;
        };
      | Ap(dir, fn, arg) =>
        let* c_fn = dhexp_of_uexp(m, fn);
        let+ c_arg = dhexp_of_uexp(m, arg);
        DHExp.Ap(dir, c_fn, c_arg) |> rewrap;
      | If(c, e1, e2) =>
        let* c' = dhexp_of_uexp(m, c);
        let* d1 = dhexp_of_uexp(m, e1);
        let+ d2 = dhexp_of_uexp(m, e2);
        // Use tag to mark inconsistent branches
        switch (err_status) {
        | InHole(Common(Inconsistent(Internal(_)))) =>
          DHExp.If(DH.Inconsistent(id, 0), c', d1, d2) |> rewrap
        | _ => DHExp.If(DH.Consistent, c', d1, d2) |> rewrap
        };
      | Match(scrut, rules) =>
        let* d_scrut = dhexp_of_uexp(m, scrut);
        let+ d_rules =
          List.map(
            ((p, e)) => {
              let* d_p = dhpat_of_upat(m, p);
              let+ d_e = dhexp_of_uexp(m, e);
              (d_p, d_e);
            },
            rules,
          )
          |> OptUtil.sequence;
        switch (err_status) {
        | InHole(Common(Inconsistent(Internal(_)))) =>
          DHExp.Match(Inconsistent(id, 0), d_scrut, d_rules) |> rewrap
        | _ => DHExp.Match(Consistent, d_scrut, d_rules) |> rewrap
        };
      | TyAlias(_, _, e) => dhexp_of_uexp(m, e)
      };
    switch (uexp.term) {
    | Parens(_) => d
    | _ => wrap(ctx, id, mode, self, d)
    };
  | Some(InfoPat(_) | InfoTyp(_) | InfoTPat(_) | Secondary(_))
  | None => None
  };
}
and dhpat_of_upat = (m: Statics.Map.t, upat: Term.UPat.t): option(DHPat.t) => {
  switch (Id.Map.find_opt(Term.UPat.rep_id(upat), m)) {
  | Some(InfoPat({mode, self, ctx, _})) =>
    let err_status = Info.status_pat(ctx, mode, self);
    let maybe_reason: option(ErrStatus.HoleReason.t) =
      switch (err_status) {
      | NotInHole(_) => None
      | InHole(_) => Some(TypeInconsistent)
      };
    let u = Term.UPat.rep_id(upat); /* NOTE: using term uids for hole ids */
    let wrap = (d: DHPat.t): option(DHPat.t) =>
      switch (maybe_reason) {
      | None => Some(d)
      | Some(reason) => Some(NonEmptyHole(reason, u, 0, d))
      };
    switch (upat.term) {
    | Invalid(t) => Some(DHPat.InvalidText(u, 0, t))
    | EmptyHole => Some(EmptyHole(u, 0))
    | MultiHole(_) =>
      // TODO: dhexp, eval for multiholes
      Some(EmptyHole(u, 0))
    | Wild => wrap(Wild)
    | Bool(b) => wrap(Bool(b))
    | Int(n) => wrap(Int(n))
    | Float(n) => wrap(Float(n))
    | String(s) => wrap(String(s))
    | Triv => wrap(Tuple([]))
    | ListLit(ps) =>
      let* ds = ps |> List.map(dhpat_of_upat(m)) |> OptUtil.sequence;
      let* ty = fixed_pat_typ(m, upat);
      wrap(ListLit(Typ.matched_list(ctx, ty), ds));
    | Constructor(name) =>
      switch (err_status) {
      | InHole(Common(NoType(FreeConstructor(_)))) =>
        Some(BadConstructor(u, 0, name))
      | _ => wrap(Constructor(name))
      }
    | Cons(hd, tl) =>
      let* d_hd = dhpat_of_upat(m, hd);
      let* d_tl = dhpat_of_upat(m, tl);
      wrap(Cons(d_hd, d_tl));
    | Tuple(ps) =>
      let* ds = ps |> List.map(dhpat_of_upat(m)) |> OptUtil.sequence;
      wrap(DHPat.Tuple(ds));
    | Var(name) => Some(Var(name))
    | Parens(p) => dhpat_of_upat(m, p)
    | Ap(p1, p2) =>
      let* d_p1 = dhpat_of_upat(m, p1);
      let* d_p2 = dhpat_of_upat(m, p2);
      wrap(Ap(d_p1, d_p2));
    | TypeAnn(p, _ty) =>
      let* dp = dhpat_of_upat(m, p);
      wrap(dp);
    };
  | Some(InfoExp(_) | InfoTyp(_) | InfoTPat(_) | Secondary(_))
  | None => None
  };
};

//let dhexp_of_uexp = Core.Memo.general(~cache_size_bound=1000, dhexp_of_uexp);

let uexp_elab = (m: Statics.Map.t, uexp: Term.UExp.t): ElaborationResult.t =>
  switch (dhexp_of_uexp(m, uexp, false)) {
  | None => DoesNotElaborate
  | Some(d) =>
    //let d = uexp_elab_wrap_builtins(d);
    let ty =
      switch (fixed_exp_typ(m, uexp)) {
      | Some(ty) => ty
      | None => Typ.Unknown(Internal)
      };
    Elaborates(d, ty, Delta.empty);
  };
