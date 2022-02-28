// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package cmd

import (
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/vespa-engine/vespa/client/go/build"
	"github.com/vespa-engine/vespa/client/go/mock"
)

func TestLog(t *testing.T) {
	homeDir := filepath.Join(t.TempDir(), ".vespa")
	pkgDir := mockApplicationPackage(t, false)
	httpClient := &mock.HTTPClient{}
	httpClient.NextResponse(200, `1632738690.905535	host1a.dev.aws-us-east-1c	806/53	logserver-container	Container.com.yahoo.container.jdisc.ConfiguredApplication	info	Switching to the latest deployed set of configurations and components. Application config generation: 52532`)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "application", "t1.a1.i1"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "target", "cloud"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"auth", "api-key"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "target", "cloud"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "api-key-file", filepath.Join(homeDir, "t1.api-key.pem")}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"auth", "cert", pkgDir}}, t, httpClient)

	out, outErr := execute(command{homeDir: homeDir, args: []string{"log", "--from", "2021-09-27T10:00:00Z", "--to", "2021-09-27T11:00:00Z"}}, t, httpClient)
	assert.Equal(t, "", outErr)

	expected := "[2021-09-27 10:31:30.905535] host1a.dev.aws-us-east-1c info    logserver-container Container.com.yahoo.container.jdisc.ConfiguredApplication	Switching to the latest deployed set of configurations and components. Application config generation: 52532\n"
	assert.Equal(t, expected, out)

	_, errOut := execute(command{homeDir: homeDir, args: []string{"log", "--from", "2021-09-27T13:12:49Z", "--to", "2021-09-27T13:15:00", "1h"}}, t, httpClient)
	assert.Equal(t, "Error: invalid period: cannot combine --from/--to with relative value: 1h\n", errOut)
}

func TestLogOldClient(t *testing.T) {
	buildVersion := build.Version
	build.Version = "7.0.0"
	homeDir := filepath.Join(t.TempDir(), ".vespa")
	pkgDir := mockApplicationPackage(t, false)
	httpClient := &mock.HTTPClient{}
	httpClient.NextResponse(200, `{"minVersion": "8.0.0"}`)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "application", "t1.a1.i1"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "target", "cloud"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"auth", "api-key"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "target", "cloud"}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"config", "set", "api-key-file", filepath.Join(homeDir, "t1.api-key.pem")}}, t, httpClient)
	execute(command{homeDir: homeDir, args: []string{"auth", "cert", pkgDir}}, t, httpClient)
	out, errOut := execute(command{homeDir: homeDir, args: []string{"log"}}, t, httpClient)
	assert.Equal(t, "", out)
	expected := "Error: client version 7.0.0 is less than the minimum supported version: 8.0.0\nHint: This is not a fatal error, but this version may not work as expected\nHint: Try 'vespa version' to check for a new version\n"
	assert.Equal(t, expected, errOut)
	build.Version = buildVersion
}
