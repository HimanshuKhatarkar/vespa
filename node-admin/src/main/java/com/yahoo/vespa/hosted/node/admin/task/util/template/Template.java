// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.node.admin.task.util.template;

/**
 * The Java representation of a template text.
 *
 * <p>A template is a sequence of literal text and dynamic sections defined by %{...} directives:</p>
 *
 * <pre>
 *     template: section*
 *     section: literal | variable | subform
 *     literal: plain text not containing %{
 *     variable: %{=identifier}
 *     if: %{if [!]identifier}template[%{else}template]%{end}
 *     subform: %{form identifier}template%{end}
 *     identifier: a valid Java identifier
 * </pre>
 *
 * <p>If the directive's end delimiter (}) is preceded by a "|" char, then any newline (\n)
 * following the end delimiter is removed.</p>
 *
 * <p>To use the template, <b>Instantiate</b> it to get a form ({@link #instantiate()}), fill it (e.g.
 * {@link Form#set(String, String) Form.set()}), and render the String ({@link Form#render()}).</p>
 *
 * <p>A form (like a template) has direct sections, and indirect sections in the body of direct subforms
 * sections (recursively). The variables that can be set for a form, are the variables defined in
 * either direct or indirect variable sections.</p>
 *
 * @see Form
 * @see TemplateParser
 * @see TemplateFile
 * @author hakonhall
 */
public class Template {
    private final Form form;

    public static Template from(String text) { return from(text, new TemplateDescriptor()); }

    public static Template from(String text, TemplateDescriptor descriptor) {
        return TemplateParser.parse(text, descriptor).template();
    }

    Template(Form form) {
        this.form = form;
    }

    public Form instantiate() { return form.copy(); }
}
