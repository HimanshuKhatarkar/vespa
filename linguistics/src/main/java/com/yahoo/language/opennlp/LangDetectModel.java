// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.language.opennlp;

import opennlp.tools.langdetect.LanguageDetectorModel;

/**
 * @author jonmv
 */
public interface LangDetectModel {

    LanguageDetectorModel load();

}
