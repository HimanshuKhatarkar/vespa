// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
// vespa document command
// author: bratseth

package cmd

import (
	"log"

	"github.com/spf13/cobra"
	"github.com/vespa-engine/vespa/util"
	"github.com/vespa-engine/vespa/vespa"
)

func init() {
	rootCmd.AddCommand(documentCmd)
	documentCmd.AddCommand(documentPutCmd)
	documentCmd.AddCommand(documentGetCmd)
	addTargetFlag(documentCmd)
}

var documentCmd = &cobra.Command{
	Use:   "document",
	Short: "Issues the document operation in the given file to Vespa",
	Long:  `TODO: Example vespa document mynamespace/mydocumenttype/myid document.json`,
	// TODO: Check args
	Run: func(cmd *cobra.Command, args []string) {
		printResult(vespa.Put("", args[0], documentTarget()), false) // TODO: Use Send
	},
}

var documentPutCmd = &cobra.Command{
	Use:   "put",
	Short: "Writes the document in the given file to Vespa",
	Long:  `TODO`,
	// TODO: Check args
	Run: func(cmd *cobra.Command, args []string) {
		if len(args) == 1 {
			printResult(vespa.Put("", args[0], documentTarget()), false)
		} else {
			printResult(vespa.Put(args[0], args[1], documentTarget()), false)
		}
	},
}

var documentGetCmd = &cobra.Command{
	Use:   "get",
	Short: "Gets a document",
	Long:  `TODO`,
	// TODO: Check args
	Run: func(cmd *cobra.Command, args []string) {
		printResult(vespa.Get(args[0], documentTarget()), true)
	},
}

func printResult(result util.OperationResult, payloadOnlyOnSuccess bool) {
	if !result.Success {
		log.Print(color.Red("Error: "), result.Message)
	} else if !(payloadOnlyOnSuccess && result.Payload != "") {
		log.Print(color.Green("Success: "), result.Message)
	}

	if result.Detail != "" {
		log.Print(color.Brown(result.Detail))
	}

	if result.Payload != "" {
		if !payloadOnlyOnSuccess {
			log.Println("")
		}
		log.Print(result.Payload)
	}
}
