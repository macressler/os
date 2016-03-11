/*++

Copyright (c) 2016 Minoca Corp. All Rights Reserved

Module Name:

    make.c

Abstract:

    This module implements output support for Make in the Minoca Build
    Generator.

Author:

    Evan Green 8-Feb-2015

Environment:

    Build

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbgen.h"

//
// ---------------------------------------------------------------- Definitions
//

#define MBGEN_MAKE_VARIABLE "$(%s)"
#define MBGEN_MAKE_LINE_CONTINUATION " \\\n        "
#define MBGEN_MAKE_INPUTS "$^"
#define MBGEN_MAKE_OUTPUT "$@"

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

VOID
MbgenMakePrintToolCommand (
    FILE *File,
    PSTR Command
    );

VOID
MbgenMakePrintTargetFile (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_TARGET Target
    );

VOID
MbgenMakePrintSource (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_SOURCE Source
    );

VOID
MbgenMakePrintTreeRoot (
    FILE *File,
    MBGEN_DIRECTORY_TREE Tree
    );

VOID
MbgenMakePrintConfig (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_TARGET Target
    );

INT
MbgenMakePrintConfigValue (
    FILE *File,
    PCHALK_OBJECT Value
    );

//
// -------------------------------------------------------------------- Globals
//

//
// ------------------------------------------------------------------ Functions
//

INT
MbgenCreateMakefile (
    PMBGEN_CONTEXT Context
    )

/*++

Routine Description:

    This routine creates a Makefile out of the build graph.

Arguments:

    Context - Supplies a pointer to the application context.

Return Value:

    0 on success.

    Returns an error number on failure.

--*/

{

    PLIST_ENTRY CurrentEntry;
    FILE *File;
    UINTN Index;
    PMBGEN_TARGET Input;
    PSTR MakefilePath;
    PMBGEN_SCRIPT Script;
    PLIST_ENTRY ScriptEntry;
    PMBGEN_SOURCE Source;
    INT Status;
    PMBGEN_TARGET Target;
    time_t Time;
    PMBGEN_TOOL Tool;

    File = NULL;
    MakefilePath = MbgenAppendPaths(Context->BuildRoot, "Makefile");
    if (MakefilePath == NULL) {
        Status = ENOMEM;
        goto CreateMakefileEnd;
    }

    if ((Context->Options & MBGEN_OPTION_VERBOSE) != 0) {
        printf("Creating %s\n", MakefilePath);
    }

    File = fopen(MakefilePath, "w");
    if (File == NULL) {
        Status = errno;
        fprintf(stderr,
                "Error: Failed to open %s: %s\n",
                MakefilePath,
                strerror(Status));

        goto CreateMakefileEnd;
    }

    Time = time(NULL);
    fprintf(File,
            "# Makefile automatically generated by mbgen at %s\n",
            ctime(&Time));

    fprintf(File, "# Define high level variables\n");
    fprintf(File, "SOURCE_ROOT := %s\n", Context->SourceRoot);
    fprintf(File, "BUILD_ROOT := %s\n\n", Context->BuildRoot);
    fprintf(File, "# Define tools\n");
    CurrentEntry = Context->ToolList.Next;
    while (CurrentEntry != &(Context->ToolList)) {
        Tool = LIST_VALUE(CurrentEntry, MBGEN_TOOL, ListEntry);
        fprintf(File, "TOOL_%s = @echo ", Tool->Name);
        MbgenMakePrintToolCommand(File, Tool->Description);
        fprintf(File, " ; \\\n    ");
        MbgenMakePrintToolCommand(File, Tool->Command);
        fprintf(File, "\n\n");
        CurrentEntry = CurrentEntry->Next;
    }

    //
    // Loop over every script (file) in the build.
    //

    fprintf(File, "\n# Define targets\n");
    ScriptEntry = Context->ScriptList.Next;
    while (ScriptEntry != &(Context->ScriptList)) {
        Script = LIST_VALUE(ScriptEntry, MBGEN_SCRIPT, ListEntry);
        ScriptEntry = ScriptEntry->Next;
        if (LIST_EMPTY(&(Script->TargetList))) {
            continue;
        }

        //
        // Loop over every target defined in the script.
        //

        if (Script->Path[0] == '\0') {
            fprintf(File, "# Define root targets\n");

        } else {
            fprintf(File, "# Define targets for %s\n", Script->Path);
        }

        CurrentEntry = Script->TargetList.Next;
        while (CurrentEntry != &(Script->TargetList)) {
            Target = LIST_VALUE(CurrentEntry, MBGEN_TARGET, ListEntry);
            CurrentEntry = CurrentEntry->Next;
            if ((Target->Flags & MBGEN_TARGET_PHONY) != 0) {
                fprintf(File, ".PHONY: ");
                MbgenMakePrintTargetFile(File, Context, Target);
                fprintf(File, "\n");
            }

            //
            // Add the configs for this target.
            //

            MbgenMakePrintConfig(File, Context, Target);
            MbgenMakePrintTargetFile(File, Context, Target);
            fprintf(File, ": ");

            //
            // Add the inputs.
            //

            for (Index = 0; Index < Target->Inputs.Count; Index += 1) {
                Input = Target->Inputs.Array[Index];
                switch (Input->Type) {
                case MbgenInputTarget:
                    MbgenMakePrintTargetFile(File, Context, Input);
                    break;

                case MbgenInputSource:
                    Source = (PMBGEN_SOURCE)Input;
                    MbgenMakePrintSource(File, Context, Source);
                    break;

                default:

                    assert(FALSE);

                    break;
                }

                if (Index + 1 != Target->Inputs.Count) {
                    fprintf(File, MBGEN_MAKE_LINE_CONTINUATION);
                }
            }

            //
            // Add the order-only inputs if there are any.
            //

            if (Target->OrderOnlyInputs.Count != 0) {
                fprintf(File, " | " MBGEN_MAKE_LINE_CONTINUATION);
                for (Index = 0;
                     Index < Target->OrderOnlyInputs.Count;
                     Index += 1) {

                    Input = Target->OrderOnlyInputs.Array[Index];
                    switch (Input->Type) {
                    case MbgenInputTarget:
                        MbgenMakePrintTargetFile(File, Context, Input);
                        break;

                    case MbgenInputSource:
                        Source = (PMBGEN_SOURCE)Input;
                        MbgenMakePrintSource(File, Context, Source);
                        break;

                    default:

                        assert(FALSE);

                        break;
                    }

                    if (Index + 1 != Target->Inputs.Count) {
                        fprintf(File, MBGEN_MAKE_LINE_CONTINUATION);
                    }
                }
            }

            //
            // Use the tool to make the target.
            //

            if (Target->Tool != NULL) {
                fprintf(File, "\n\t$(TOOL_%s)\n\n", Target->Tool);

            } else {
                fprintf(File, "\n\n");
            }
        }
    }

    Status = 0;

CreateMakefileEnd:
    if (File != NULL) {
        fclose(File);
    }

    if (MakefilePath != NULL) {
        free(MakefilePath);
    }

    return Status;
}

//
// --------------------------------------------------------- Internal Functions
//

VOID
MbgenMakePrintToolCommand (
    FILE *File,
    PSTR Command
    )

/*++

Routine Description:

    This routine prints a tool command or description, converting variable
    expressions into proper make format.

Arguments:

    File - Supplies a pointer to the file to print to.

    Command - Supplies a pointer to the command to convert.

Return Value:

    None.

--*/

{

    PSTR Copy;
    CHAR Original;
    PSTR Variable;

    Copy = strdup(Command);
    if (Copy == NULL) {
        return;
    }

    Command = Copy;
    while (*Command != '\0') {
        if (*Command != '$') {
            fputc(*Command, File);
            Command += 1;
            continue;
        }

        Command += 1;

        //
        // A double dollar is just a literal dollar sign.
        //

        if (*Command == '$') {
            fputc(*Command, File);
            Command += 1;
            continue;
        }

        //
        // A dollar sign plus some non-variable-name character is also just
        // passed over literally.
        //

        if (!MBGEN_IS_NAME0(*Command)) {
            fputc('$', File);
            fputc(*Command, File);
            Command += 1;
            continue;
        }

        //
        // Get to the end of the variable name.
        //

        Variable = Command;
        while (MBGEN_IS_NAME(*Command)) {
            Command += 1;
        }

        //
        // Temporarily terminate the name, and compare it against the special
        // IN and OUT variables, which substitute differently.
        //

        Original = *Command;
        *Command = '\0';
        if (strcasecmp(Variable, "IN") == 0) {
            fprintf(File, "%s", MBGEN_MAKE_INPUTS);

        } else if (strcasecmp(Variable, "OUT") == 0) {
            fprintf(File, "%s", MBGEN_MAKE_OUTPUT);

        //
        // Print the variable reference in the normal make way.
        //

        } else {
            fprintf(File, MBGEN_MAKE_VARIABLE, Variable);
        }

        *Command = Original;
    }

    free(Copy);
    return;
}

VOID
MbgenMakePrintTargetFile (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_TARGET Target
    )

/*++

Routine Description:

    This routine prints a target's output file name.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Target - Supplies a pointer to the target to print.

Return Value:

    None.

--*/

{

    PMBGEN_SCRIPT Script;

    Script = Target->Script;
    if ((Target->Flags & MBGEN_TARGET_PHONY) != 0) {
        fprintf(File, "%s", Target->Output);
        return;
    }

    MbgenMakePrintTreeRoot(File, Target->Tree);
    fprintf(File, "/%s/%s", Script->Path, Target->Output);
    return;
}

VOID
MbgenMakePrintSource (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_SOURCE Source
    )

/*++

Routine Description:

    This routine prints a source's file name.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Source - Supplies a pointer to the source file information.

Return Value:

    None.

--*/

{

    MbgenMakePrintTreeRoot(File, Source->Tree);
    fprintf(File, "/%s", Source->Path);
    return;
}

VOID
MbgenMakePrintTreeRoot (
    FILE *File,
    MBGEN_DIRECTORY_TREE Tree
    )

/*++

Routine Description:

    This routine prints the tree root shorthand for the given tree.

Arguments:

    File - Supplies a pointer to the file to print to.

    Tree - Supplies the directory tree root to print.

Return Value:

    None.

--*/

{

    switch (Tree) {
    case MbgenSourceTree:
        fprintf(File, MBGEN_MAKE_VARIABLE, "SOURCE_ROOT");
        break;

    case MbgenBuildTree:
        fprintf(File, MBGEN_MAKE_VARIABLE, "BUILD_ROOT");
        break;

    case MbgenAbsolutePath:
        break;

    default:

        assert(FALSE);

        break;
    }

    return;
}

VOID
MbgenMakePrintConfig (
    FILE *File,
    PMBGEN_CONTEXT Context,
    PMBGEN_TARGET Target
    )

/*++

Routine Description:

    This routine prints a target's configuration dictionary.

Arguments:

    File - Supplies a pointer to the file to print to.

    Context - Supplies a pointer to the application context.

    Target - Supplies a pointer to the target whose configuration should be
        printed.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT Config;
    PLIST_ENTRY CurrentEntry;
    PCHALK_DICT_ENTRY Entry;
    INT Status;
    PCHALK_OBJECT Value;

    Config = Target->Config;
    if (Config == NULL) {
        return;
    }

    assert(Config->Header.Type == ChalkObjectDict);

    CurrentEntry = Config->Dict.EntryList.Next;
    while (CurrentEntry != &(Config->Dict.EntryList)) {
        Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        Value = Entry->Value;
        if (Entry->Key->Header.Type != ChalkObjectString) {
            fprintf(stderr,
                    "Error: Skipping config object with non-string key.\n");

            continue;
        }

        if ((Value->Header.Type != ChalkObjectString) &&
            (Value->Header.Type != ChalkObjectInteger) &&
            (Value->Header.Type != ChalkObjectList)) {

            fprintf(stderr,
                    "Error: Skipping config key %s: unsupported type.\n",
                    Entry->Key->String.String);

            continue;
        }

        MbgenMakePrintTargetFile(File, Context, Target);
        fprintf(File, ": ");
        fprintf(File, "%s := ", Entry->Key->String.String);
        Status = MbgenMakePrintConfigValue(File, Value);
        if (Status != 0) {
            fprintf(stderr,
                    "Error: Skipping some values for key %s.\n",
                    Entry->Key->String.String);
        }

        fprintf(File, "\n");
    }

    return;
}

INT
MbgenMakePrintConfigValue (
    FILE *File,
    PCHALK_OBJECT Value
    )

/*++

Routine Description:

    This routine prints a configuration value.

Arguments:

    File - Supplies a pointer to the file to print to.

    Value - Supplies a pointer to the object to print.

Return Value:

    0 on success.

    -1 if some entries were skipped.

--*/

{

    UINTN Index;
    INT Status;
    INT TotalStatus;

    TotalStatus = 0;
    if (Value->Header.Type == ChalkObjectList) {

        //
        // Recurse to print every object on the list, separated by a space.
        //

        for (Index = 0; Index < Value->List.Count; Index += 1) {
            Status = MbgenMakePrintConfigValue(File, Value->List.Array[Index]);
            if (Status != 0) {
                TotalStatus = Status;
            }

            if (Index != Value->List.Count - 1) {
                fprintf(File, " ");
            }
        }

    } else if (Value->Header.Type == ChalkObjectInteger) {
        fprintf(File, "%lld", Value->Integer.Value);

    } else if (Value->Header.Type == ChalkObjectString) {
        fprintf(File, "%s", Value->String.String);

    } else {
        TotalStatus = -1;
    }

    return TotalStatus;
}
