open Haz3lcore;
open Virtual_dom.Vdom;
open Node;

type vis_marked('a) =
  | InstructorOnly(unit => 'a)
  | Always('a);

let render_cells = (settings: Settings.t, v: list(vis_marked(Node.t))) => {
  List.filter_map(
    vis =>
      switch (vis) {
      | InstructorOnly(f) => settings.instructor_mode ? Some(f()) : None
      | Always(node) => Some(node)
      },
    v,
  );
};

let view =
    (
      ~inject,
      ~ui_state: Model.ui_state,
      ~settings: Settings.t,
      ~exercise,
      ~results,
      ~color_highlighting,
    ) => {
  let Exercise.{eds, pos} = exercise;
  let stitched_dynamics =
    Exercise.stitch_dynamic(
      settings.core,
      exercise,
      settings.core.dynamics ? Some(results) : None,
    );
  let {
    test_validation,
    user_impl,
    user_tests,
    prelude,
    instructor,
    hidden_bugs,
    hidden_tests: _,
  }:
    Exercise.stitched(Exercise.DynamicsItem.t) = stitched_dynamics;

  let grading_report = Grading.GradingReport.mk(eds, ~stitched_dynamics);

  let score_view = Grading.GradingReport.view_overall_score(grading_report);

  let editor_view =
      (
        ~editor: Editor.t,
        ~info_map,
        ~caption: string,
        ~subcaption: option(string)=?,
        ~test_results,
        ~footer=?,
        this_pos,
      ) => {
    Cell.editor_view(
      ~selected=pos == this_pos,
      ~error_ids=Editor.error_ids(editor.state.meta.term_ranges, info_map),
      ~inject,
      ~ui_state,
      ~mousedown_updates=[SwitchEditor(this_pos)],
      ~settings,
      ~color_highlighting,
      ~caption=Cell.caption(caption, ~rest=?subcaption),
      ~code_id=Exercise.show_pos(this_pos),
      ~test_results,
      ~footer,
      Editor.get_syntax(editor),
    );
  };

  let title_view = Cell.title_cell(eds.title);

  let prompt_view =
    Cell.narrative_cell(
      div(~attr=Attr.class_("cell-prompt"), [eds.prompt]),
    );

  let prelude_view =
    Always(
      editor_view(
        Prelude,
        ~caption="Prelude",
        ~subcaption=settings.instructor_mode ? "" : " (Read-Only)",
        ~editor=eds.prelude,
        ~info_map=prelude.info_map,
        ~test_results=ModelResult.unwrap_test_results(prelude.simple_result),
      ),
    );

  let correct_impl_view =
    InstructorOnly(
      () =>
        editor_view(
          CorrectImpl,
          ~caption="Correct Implementation",
          ~editor=eds.correct_impl,
          ~info_map=instructor.info_map,
          ~test_results=
            ModelResult.unwrap_test_results(instructor.simple_result),
        ),
    );

  // determine trailing hole
  // TODO: module
  let correct_impl_ctx_view =
    Always(
      {
        let exp_ctx_view = {
          let correct_impl_trailing_hole_ctx =
            Haz3lcore.Editor.trailing_hole_ctx(
              eds.correct_impl,
              instructor.info_map,
            );
          let prelude_trailing_hole_ctx =
            Haz3lcore.Editor.trailing_hole_ctx(eds.prelude, prelude.info_map);
          switch (correct_impl_trailing_hole_ctx, prelude_trailing_hole_ctx) {
          | (None, _) => Node.div([text("No context available (1)")])
          | (_, None) => Node.div([text("No context available (2)")]) // TODO show exercise configuration error
          | (
              Some(correct_impl_trailing_hole_ctx),
              Some(prelude_trailing_hole_ctx),
            ) =>
            let specific_ctx =
              Haz3lcore.Ctx.subtract_prefix(
                correct_impl_trailing_hole_ctx,
                prelude_trailing_hole_ctx,
              );
            switch (specific_ctx) {
            | None => Node.div([text("No context available")]) // TODO show exercise configuration error
            | Some(specific_ctx) =>
              CtxInspector.ctx_view(~inject, specific_ctx)
            };
          };
        };
        Cell.simple_cell_view([
          Cell.simple_cell_item([
            Cell.caption(
              "Correct Implementation",
              ~rest=" (Type Signatures Only)",
            ),
            exp_ctx_view,
          ]),
        ]);
      },
    );

  let your_tests_view =
    Always(
      editor_view(
        YourTestsValidation,
        ~caption="Test Validation",
        ~subcaption=": Your Tests vs. Correct Implementation",
        ~editor=eds.your_tests.tests,
        ~info_map=test_validation.info_map,
        ~test_results=
          ModelResult.unwrap_test_results(test_validation.simple_result),
        ~footer=
          Grading.TestValidationReport.view(
            ~inject,
            grading_report.test_validation_report,
            grading_report.point_distribution.test_validation,
          ),
      ),
    );

  let wrong_impl_views =
    List.mapi(
      (
        i,
        (
          Exercise.{impl, _},
          Exercise.DynamicsItem.{info_map, simple_result, _},
        ),
      ) => {
        InstructorOnly(
          () =>
            editor_view(
              HiddenBugs(i),
              ~caption="Wrong Implementation " ++ string_of_int(i + 1),
              ~editor=impl,
              ~info_map,
              ~test_results=ModelResult.unwrap_test_results(simple_result),
            ),
        )
      },
      List.combine(eds.hidden_bugs, hidden_bugs),
    );

  let mutation_testing_view =
    Always(
      Grading.MutationTestingReport.view(
        ~inject,
        grading_report.mutation_testing_report,
        grading_report.point_distribution.mutation_testing,
      ),
    );

  let your_impl_view =
    Always(
      editor_view(
        YourImpl,
        ~caption="Your Implementation",
        ~editor=eds.your_impl,
        ~info_map=user_impl.info_map,
        ~test_results=
          ModelResult.unwrap_test_results(user_impl.simple_result),
        ~footer=
          Cell.footer(
            ~settings,
            ~inject,
            ~ui_state,
            ModelResult.unwrap'(user_impl.simple_result),
          ),
      ),
    );

  let testing_results =
    ModelResult.unwrap_test_results(user_tests.simple_result);

  let syntax_grading_view =
    Always(Grading.SyntaxReport.view(grading_report.syntax_report));

  let impl_validation_view =
    Always(
      editor_view(
        YourTestsTesting,
        ~caption="Implementation Validation",
        ~subcaption=
          ": Your Tests (code synchronized with Test Validation cell above) vs. Your Implementation",
        ~editor=eds.your_tests.tests,
        ~info_map=user_tests.info_map,
        ~test_results=testing_results,
        ~footer=
          Cell.test_report_footer_view(
            ~inject,
            ~test_results=testing_results,
          ),
      ),
    );

  let hidden_tests_view =
    InstructorOnly(
      () =>
        editor_view(
          HiddenTests,
          ~caption="Hidden Tests",
          ~editor=eds.hidden_tests.tests,
          ~info_map=instructor.info_map,
          ~test_results=
            ModelResult.unwrap_test_results(instructor.simple_result),
        ),
    );

  let impl_grading_view =
    Always(
      Grading.ImplGradingReport.view(
        ~inject,
        ~report=grading_report.impl_grading_report,
        ~syntax_report=grading_report.syntax_report,
        ~max_points=grading_report.point_distribution.impl_grading,
      ),
    );

  [score_view, title_view, prompt_view]
  @ render_cells(
      settings,
      [
        prelude_view,
        correct_impl_view,
        correct_impl_ctx_view,
        your_tests_view,
      ]
      @ wrong_impl_views
      @ [
        mutation_testing_view,
        your_impl_view,
        syntax_grading_view,
        impl_validation_view,
        hidden_tests_view,
        impl_grading_view,
      ],
    );
};

let reset_button = inject =>
  Widgets.button_named(
    Icons.trash,
    _ => {
      let confirmed =
        JsUtil.confirm(
          "Are you SURE you want to reset this exercise? You will lose any existing code that you have written, and course staff have no way to restore it!",
        );
      if (confirmed) {
        inject(UpdateAction.ResetCurrentEditor);
      } else {
        Virtual_dom.Vdom.Effect.Ignore;
      };
    },
    ~tooltip="Reset Exercise",
  );

let instructor_export = (exercise: Exercise.state) =>
  Widgets.button_named(
    Icons.star,
    _ => {
      // .ml files because show uses OCaml syntax (dune handles seamlessly)
      let module_name = exercise.eds.module_name;
      let filename = exercise.eds.module_name ++ ".ml";
      let content_type = "text/plain";
      let contents = Exercise.export_module(module_name, exercise);
      JsUtil.download_string_file(~filename, ~content_type, ~contents);
      Virtual_dom.Vdom.Effect.Ignore;
    },
    ~tooltip="Export Exercise Module",
  );

let instructor_transitionary_export = (exercise: Exercise.state) =>
  Widgets.button_named(
    Icons.star,
    _ => {
      // .ml files because show uses OCaml syntax (dune handles seamlessly)
      let module_name = exercise.eds.module_name;
      let filename = exercise.eds.module_name ++ ".ml";
      let content_type = "text/plain";
      let contents =
        Exercise.export_transitionary_module(module_name, exercise);
      JsUtil.download_string_file(~filename, ~content_type, ~contents);
      Virtual_dom.Vdom.Effect.Ignore;
    },
    ~tooltip="Export Transitionary Exercise Module",
  );

let instructor_grading_export = (exercise: Exercise.state) =>
  Widgets.button_named(
    Icons.star,
    _ => {
      // .ml files because show uses OCaml syntax (dune handles seamlessly)
      let module_name = exercise.eds.module_name;
      let filename = exercise.eds.module_name ++ "_grading.ml";
      let content_type = "text/plain";
      let contents = Exercise.export_grading_module(module_name, exercise);
      JsUtil.download_string_file(~filename, ~content_type, ~contents);
      Virtual_dom.Vdom.Effect.Ignore;
    },
    ~tooltip="Export Grading Exercise Module",
  );

let download_editor_state = (~instructor_mode) =>
  Log.get_and(log => {
    let data = Export.export_all(~instructor_mode, ~log);
    JsUtil.download_json(ExerciseSettings.filename, data);
  });

let export_submission = (~settings: Settings.t) =>
  Widgets.button_named(
    Icons.star,
    _ => {
      download_editor_state(~instructor_mode=settings.instructor_mode);
      Virtual_dom.Vdom.Effect.Ignore;
    },
    ~tooltip="Export Submission",
  );

let import_submission = (~inject) =>
  Widgets.file_select_button_named(
    "import-submission",
    Icons.star,
    file => {
      switch (file) {
      | None => Virtual_dom.Vdom.Effect.Ignore
      | Some(file) => inject(UpdateAction.InitImportAll(file))
      }
    },
    ~tooltip="Import Submission",
  );
