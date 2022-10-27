// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.security.tool;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * @author vekterli
 */
public class CryptoToolsTest {

    private static record ProcessOutput(int exitCode, String stdOut, String stdErr) {}

    @TempDir
    public File tmpFolder;

    private void verifyStdoutMatchesFile(List<String> args, String expectedFile) throws IOException {
        var procOut = runMain(args, Map.of());
        assertEquals(0, procOut.exitCode());
        assertEquals(readTestResource(expectedFile), procOut.stdOut());
    }

    private void verifyStderrEquals(List<String> args, String expectedMessage) throws IOException {
        var procOut = runMain(args, Map.of());
        assertEquals(1, procOut.exitCode()); // Assume checking stderr is because of a failure.
        assertEquals(expectedMessage, procOut.stdErr());
    }

    @Test
    void top_level_help_page_printed_if_help_option_given() throws IOException {
        verifyStdoutMatchesFile(List.of("--help"), "expected-help-output.txt");
    }

    @Test
    void top_level_help_page_printed_if_no_option_given() throws IOException {
        verifyStdoutMatchesFile(List.of(), "expected-help-output.txt");
    }

    @Test
    void keygen_help_printed_if_help_option_given_to_subtool() throws IOException {
        verifyStdoutMatchesFile(List.of("keygen", "--help"), "expected-keygen-help-output.txt");
    }

    @Test
    void encrypt_help_printed_if_help_option_given_to_subtool() throws IOException {
        verifyStdoutMatchesFile(List.of("encrypt", "--help"), "expected-encrypt-help-output.txt");
    }

    @Test
    void decrypt_help_printed_if_help_option_given_to_subtool() throws IOException {
        verifyStdoutMatchesFile(List.of("decrypt", "--help"), "expected-decrypt-help-output.txt");
    }

    @Test
    void missing_required_parameter_prints_error_message() throws IOException {
        // We don't test all possible input arguments to all tools, since it'd be too closely
        // bound to the order in which the implementation checks for argument presence.
        // This primarily verifies that IllegalArgumentExceptions thrown by a tool will be caught
        // and printed to stderr as expected.
        verifyStderrEquals(List.of("keygen"),
                "Invalid command line arguments: Required argument '--private-out-file' must be provided\n");
        verifyStderrEquals(List.of("keygen", "--private-out-file", "foo.txt"),
                "Invalid command line arguments: Required argument '--public-out-file' must be provided\n");
    }

    // We don't want to casually overwrite key material if someone runs a command twice by accident.
    @Test
    void keygen_fails_by_default_if_output_file_exists() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Path pubKeyFile  = pathInTemp("pub.txt");
        Files.writeString(privKeyFile, TEST_PRIV_KEY);

        verifyStderrEquals(List.of("keygen",
                                   "--private-out-file", absPathOf(privKeyFile),
                                   "--public-out-file",  absPathOf(pubKeyFile)),
                ("Invalid command line arguments: Output file '%s' already exists. No keys written. " +
                 "If you want to overwrite existing files, specify --overwrite-existing.\n")
                .formatted(absPathOf(privKeyFile)));

        Files.delete(privKeyFile);
        Files.writeString(pubKeyFile, TEST_PUB_KEY);

        verifyStderrEquals(List.of("keygen",
                                   "--private-out-file", absPathOf(privKeyFile),
                                   "--public-out-file",  absPathOf(pubKeyFile)),
                ("Invalid command line arguments: Output file '%s' already exists. No keys written. " +
                 "If you want to overwrite existing files, specify --overwrite-existing.\n")
                 .formatted(absPathOf(pubKeyFile)));
    }

    // ... but we'll allow it if someone enables the foot-gun option.
    @Test
    void keygen_allowed_if_output_file_exists_and_explicit_overwrite_option_specified() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Path pubKeyFile  = pathInTemp("pub.txt");
        Files.writeString(privKeyFile, TEST_PRIV_KEY);
        Files.writeString(pubKeyFile,  TEST_PUB_KEY);

        var procOut = runMain(List.of("keygen",
                                     "--private-out-file", absPathOf(privKeyFile),
                                     "--public-out-file",  absPathOf(pubKeyFile),
                                     "--overwrite-existing"));
        assertEquals(0, procOut.exitCode());

        // Keys are random, so we don't know what they'll end up being. But the likelihood of them
        // exactly matching the test keys is effectively and realistically zero.
        assertNotEquals(TEST_PRIV_KEY, Files.readString(privKeyFile));
        assertNotEquals(TEST_PUB_KEY,  Files.readString(pubKeyFile));
    }

    @Test
    void keygen_writes_private_key_with_user_only_rw_permissions() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Path pubKeyFile  = pathInTemp("pub.txt");

        var procOut = runMain(List.of("keygen",
                                      "--private-out-file", absPathOf(privKeyFile),
                                      "--public-out-file",  absPathOf(pubKeyFile)));
        assertEquals(0, procOut.exitCode());
        var privKeyPerms  = Files.getPosixFilePermissions(privKeyFile);
        var expectedPerms = PosixFilePermissions.fromString("rw-------");
        assertEquals(expectedPerms, privKeyPerms);
    }

    private static final String TEST_PRIV_KEY     = "4qGcntygFn_a3uqeBa1PbDlygQ-cpOuNznTPIz9ftWE";
    private static final String TEST_PUB_KEY      = "ROAH_S862tNMpbJ49lu1dPXFCPHFIXZK30pSrMZEmEg";
    // Token created for the above public key (matching the above private key), using key id 1
    private static final String TEST_TOKEN        = "AQAAAQAgwyxd7bFNQB_2LdL3bw-xFlvrxXhs7WWNVCKZ4" +
                                                    "EFeNVtu42JMwM74bMN4E46v6mYcfQNPzcMGaP22Wl2cTnji0A";
    private static final int    TEST_TOKEN_KEY_ID = 1;

    @Test
    void encrypt_fails_with_error_message_if_no_input_file_is_given() throws IOException {
        verifyStderrEquals(List.of("encrypt",
                                   "--output-file",          "foo",
                                   "--recipient-public-key", TEST_PUB_KEY,
                                   "--key-id",               "1234"),
                "Invalid command line arguments: Expected exactly 1 file argument to encrypt\n");
    }

    @Test
    void encrypt_fails_with_error_message_if_input_file_does_not_exist() throws IOException {
        verifyStderrEquals(List.of("encrypt",
                                   "no-such-file",
                                   "--output-file",          "foo",
                                   "--recipient-public-key", TEST_PUB_KEY,
                                   "--key-id",               "1234"),
                "Invalid command line arguments: Cannot encrypt file 'no-such-file' as it does not exist\n");
    }

    @Test
    void decrypt_fails_with_error_message_if_no_input_file_is_given() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Files.writeString(privKeyFile, TEST_PRIV_KEY);

        verifyStderrEquals(List.of("decrypt",
                                   "--output-file",                "foo",
                                   "--recipient-private-key-file", absPathOf(privKeyFile),
                                   "--token",                      TEST_TOKEN,
                                   "--key-id",                     Integer.toString(TEST_TOKEN_KEY_ID)),
                "Invalid command line arguments: Expected exactly 1 file argument to decrypt\n");
    }

    @Test
    void decrypt_fails_with_error_message_if_input_file_does_not_exist() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Files.writeString(privKeyFile, TEST_PRIV_KEY);

        verifyStderrEquals(List.of("decrypt",
                                   "no-such-file",
                                   "--output-file",                "foo",
                                   "--recipient-private-key-file", absPathOf(privKeyFile),
                                   "--token",                      TEST_TOKEN,
                                   "--key-id",                     Integer.toString(TEST_TOKEN_KEY_ID)),
                "Invalid command line arguments: Cannot decrypt file 'no-such-file' as it does not exist\n");
    }

    @Test
    void decrypt_fails_with_error_message_if_expected_key_id_does_not_match_key_id_in_token() throws IOException {
        Path privKeyFile = pathInTemp("priv.txt");
        Files.writeString(privKeyFile, TEST_PRIV_KEY);

        Path inputFile = pathInTemp("input.txt");
        Files.writeString(inputFile, "dummy-not-actually-encrypted-data");

        verifyStderrEquals(List.of("decrypt",
                                   absPathOf(inputFile),
                                   "--output-file",                "foo",
                                   "--recipient-private-key-file", absPathOf(privKeyFile),
                                   "--token",                      TEST_TOKEN,
                                   "--key-id",                     Integer.toString(TEST_TOKEN_KEY_ID + 1)),
                "Invalid command line arguments: Key ID specified with --key-id (2) does not match " +
                        "key ID used when generating the supplied token (1)\n");
    }

    @Test
    void can_end_to_end_keygen_encrypt_and_decrypt() throws IOException {
        String greatSecret = "Dogs can't look up";

        Path secretFile = pathInTemp("secret.txt");
        Files.writeString(secretFile, greatSecret);

        var privPath = pathInTemp("priv.txt");
        var pubPath = pathInTemp("pub.txt");
        var procOut = runMain(List.of(
                "keygen",
                "--private-out-file", absPathOf(privPath),
                "--public-out-file",  absPathOf(pubPath)));
        assertEquals(0, procOut.exitCode());
        assertEquals("", procOut.stdOut());
        assertEquals("", procOut.stdErr());

        assertTrue(privPath.toFile().exists());
        assertTrue(pubPath.toFile().exists());

        var encryptedPath = pathInTemp("encrypted.bin");
        // TODO support (and test) public key via file
        procOut = runMain(List.of(
                "encrypt",
                absPathOf(secretFile),
                "--output-file",          absPathOf(encryptedPath),
                "--recipient-public-key", Files.readString(pubPath),
                "--key-id",               "1234"));
        assertEquals(0, procOut.exitCode());
        assertEquals("", procOut.stdErr());

        var token = procOut.stdOut();
        assertFalse(token.isBlank());

        assertTrue(encryptedPath.toFile().exists());

        var decryptedPath = pathInTemp("decrypted.txt");
        procOut = runMain(List.of(
                "decrypt",
                absPathOf(encryptedPath),
                "--output-file",                absPathOf(decryptedPath),
                "--recipient-private-key-file", absPathOf(privPath),
                "--key-id",                     "1234",
                "--token",                      token
                ));
        assertEquals(0, procOut.exitCode());
        assertEquals("", procOut.stdOut());
        assertEquals("", procOut.stdErr());

        assertEquals(greatSecret, Files.readString(decryptedPath));
    }

    private ProcessOutput runMain(List<String> args) {
        // Expect that this is used for running a command that is not supposed to fail. But if it does,
        // include exception trace in stderr to make it easier to debug.
        return runMain(args, Map.of("VESPA_DEBUG", "true"));
    }

    private ProcessOutput runMain(List<String> args, Map<String, String> env) {
        var stdOutBytes = new ByteArrayOutputStream();
        var stdErrBytes = new ByteArrayOutputStream();
        var stdOut      = new PrintStream(stdOutBytes);
        var stdError    = new PrintStream(stdErrBytes);

        int exitCode = new Main(stdOut, stdError).execute(args.toArray(new String[0]), env);

        stdOut.flush();
        stdError.flush();

        return new ProcessOutput(exitCode, stdOutBytes.toString(), stdErrBytes.toString());
    }

    private static String readTestResource(String fileName) throws IOException {
        return Files.readString(Paths.get(CryptoToolsTest.class.getResource('/' + fileName).getFile()));
    }

    private Path pathInTemp(String fileName) {
        return tmpFolder.toPath().resolve(fileName);
    }

    private static String absPathOf(Path path) {
        return path.toAbsolutePath().toString();
    }

}