import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

/** Contributor check: user-facing JSX and literal message calls must stay localized. */
export function sourceErrors(
  english: Record<string, unknown>,
  root = fileURLToPath(new URL('../../../apps/web/src/', import.meta.url)),
): string[] {
  const errors: string[] = [];
  const files = (dir: string): string[] =>
    readdirSync(dir, { withFileTypes: true }).flatMap((entry) =>
      entry.isDirectory() ? files(join(dir, entry.name)) : [join(dir, entry.name)],
    );
  const calls = new Set(['t', 'translate', 'tp', 'sourceMessage', 'message', 'validationMessage']);
  const attributes = new Set(['alt', 'title', 'aria-label', 'placeholder']);
  // This syntax example is entered literally into the technical terrain filter.
  const technicalText = new Set(['terrain:natural,feature:lakes', 'ai.js', 'CC-BY-4.0']);
  for (const file of files(root).filter((file) => /\.tsx?$/.test(file))) {
    const tree = ts.createSourceFile(
      file,
      readFileSync(file, 'utf8'),
      ts.ScriptTarget.Latest,
      true,
    );
    const report = (node: ts.Node, text: string, kind: string) => {
      const line = tree.getLineAndCharacterOfPosition(node.getStart(tree)).line + 1;
      errors.push(`${file.slice(root.length)}:${line}: ${kind}: ${text.slice(0, 120)}`);
    };
    const literal = (node: ts.Node) => {
      if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) return node.text;
      return undefined;
    };
    const renderedCopy = (node: ts.Expression): void => {
      if (ts.isConditionalExpression(node)) {
        renderedCopy(node.whenTrue);
        renderedCopy(node.whenFalse);
      } else if (ts.isBinaryExpression(node)) {
        if (node.operatorToken.kind !== ts.SyntaxKind.AmpersandAmpersandToken)
          renderedCopy(node.left);
        renderedCopy(node.right);
      } else if (ts.isParenthesizedExpression(node)) renderedCopy(node.expression);
      else if (ts.isArrayLiteralExpression(node)) {
        for (const item of node.elements) if (ts.isExpression(item)) renderedCopy(item);
      } else if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) {
        if (/[a-zA-Z]{2}/.test(node.text) && !technicalText.has(node.text))
          report(node, node.text, 'unlocalized JSX text');
      } else if (ts.isTemplateExpression(node)) {
        const text = node.head.text + node.templateSpans.map((span) => span.literal.text).join('');
        if (/[a-zA-Z]{2}/.test(text)) report(node, text, 'unlocalized JSX template');
      }
    };
    const visit = (node: ts.Node) => {
      if (
        (ts.isCallExpression(node) && calls.has(node.expression.getText(tree))) ||
        (ts.isNewExpression(node) && node.expression.getText(tree) === 'MessageError')
      ) {
        for (const argument of (node.arguments ?? []).slice(
          0,
          node.expression.getText(tree) === 'tp' ? 2 : 1,
        )) {
          const text = literal(argument);
          if (text !== undefined && !Object.hasOwn(english, text))
            report(argument, text, 'missing catalog key');
          if (text && /^(?:https?:|\/(?:api|play|maps|admin)(?:\/|$))/.test(text))
            report(argument, text, 'technical URL translated');
        }
      }
      if (ts.isJsxAttribute(node) && ['source', 'singular'].includes(node.name.getText(tree))) {
        const opening = node.parent.parent;
        if (
          (ts.isJsxOpeningElement(opening) || ts.isJsxSelfClosingElement(opening)) &&
          opening.tagName.getText(tree) === 'RichMessage' &&
          node.initializer
        ) {
          const source =
            literal(node.initializer) ??
            (ts.isJsxExpression(node.initializer) && node.initializer.expression
              ? literal(node.initializer.expression)
              : undefined);
          if (source !== undefined && !Object.hasOwn(english, source))
            report(node, source, 'missing rich-message catalog key');
        }
      }
      if (
        ts.isJsxAttribute(node) &&
        node.name.getText(tree) === 'slots' &&
        node.initializer &&
        ts.isJsxExpression(node.initializer) &&
        node.initializer.expression &&
        ts.isObjectLiteralExpression(node.initializer.expression)
      ) {
        const opening = node.parent.parent;
        if (
          (ts.isJsxOpeningElement(opening) || ts.isJsxSelfClosingElement(opening)) &&
          opening.tagName.getText(tree) === 'RichMessage'
        ) {
          for (const property of node.initializer.expression.properties)
            if (ts.isPropertyAssignment(property)) renderedCopy(property.initializer);
        }
      }
      let text: string | undefined;
      if (ts.isJsxText(node)) text = node.text.trim();
      if (ts.isJsxAttribute(node) && attributes.has(node.name.getText(tree)) && node.initializer)
        text = literal(node.initializer);
      if (ts.isJsxExpression(node) && node.expression) {
        if (!ts.isJsxAttribute(node.parent) || attributes.has(node.parent.name.getText(tree)))
          renderedCopy(node.expression);
      }
      if (text && /[a-zA-Z]{2}/.test(text) && !technicalText.has(text))
        report(node, text, 'unlocalized JSX text');
      ts.forEachChild(node, visit);
    };
    visit(tree);
  }
  return errors;
}
