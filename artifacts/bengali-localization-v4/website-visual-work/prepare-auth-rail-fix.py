from pathlib import Path
import difflib,json,hashlib
root=Path.cwd();out=root/'artifacts/bengali-localization-v4/website-visual-work';changes={}
f=root/'platform/apps/web/src/App.tsx';s=f.read_text();old='''      <a className="btn primary small" href="/signin">
        {t('Sign in')}
      </a>''';new='''      <a
        className="btn primary small sidebar-auth"
        href="/signin"
        aria-label={t('Sign in')}
        title={t('Sign in')}
      >
        <Icon name="users" size={20} className="sidebar-auth-icon" aria-hidden="true" />
        <span className="sidebar-auth-label">{t('Sign in')}</span>
      </a>''';assert s.count(old)==1;s=s.replace(old,new)
old='''      <button className="small" onClick={() => void signOut()}>
        {t('Sign out')}
      </button>''';new='''      <button
        className="small sidebar-auth"
        onClick={() => void signOut()}
        aria-label={t('Sign out')}
        title={t('Sign out')}
      >
        <Icon name="x" size={20} className="sidebar-auth-icon" aria-hidden="true" />
        <span className="sidebar-auth-label">{t('Sign out')}</span>
      </button>''';assert s.count(old)==1;s=s.replace(old,new);changes[f]=s
f=root/'platform/apps/web/src/styles/layout.css';s=f.read_text();anchor='''.sidebar-account.on .chip {
  border-color: var(--accent);
}
''';assert s.count(anchor)==1;s=s.replace(anchor,anchor+'''.sidebar-auth-icon {
  display: none;
}
''')
for prefix,indent in [('.sidebar-collapsed .app-sidebar',''),('.app-sidebar','  ')]:
 old=indent+prefix+''' .sidebar-account :is(button, .btn) {
'''+indent+'''  font-size: var(--text-xs);
'''+indent+'''  padding: 0 2px;
'''+indent+'''  max-width: 100%;
'''+indent+'''  white-space: normal;
'''+indent+'''  overflow-wrap: anywhere;
'''+indent+'''}'''
 new=indent+prefix+''' .sidebar-account .sidebar-auth {
'''+indent+'''  width: 44px;
'''+indent+'''  min-width: 44px;
'''+indent+'''  min-height: 44px;
'''+indent+'''  padding: 0;
'''+indent+'''}
'''+indent+prefix+''' .sidebar-auth-icon {
'''+indent+'''  display: block;
'''+indent+'''}
'''+indent+prefix+''' .sidebar-auth-label {
'''+indent+'''  display: none;
'''+indent+'''}''';assert s.count(old)==1,(prefix,old);s=s.replace(old,new)
changes[f]=s
f=root/'platform/apps/web/e2e/i18n.spec.ts';s=f.read_text();anchor='''    // Long translations and RTL layout must fit the viewport on phones as well as desktops.
''';assert s.count(anchor)==1;extra='''    if (locale.code === 'bn' && (page.viewportSize()?.width ?? 1280) < 1100) {
      // The narrow rail must use an icon, while retaining the full translated
      // action name for assistive technology and expanded navigation.
      const signInLabel = catalog['Sign in'];
      if (!signInLabel) throw new Error('Missing Bengali sign-in label');
      await expect(signIn).toHaveAccessibleName(signInLabel);
      const icon = signIn.locator('.sidebar-auth-icon');
      await expect(icon).toBeVisible();
      await expect(signIn.locator('.sidebar-auth-label')).toBeHidden();
      const buttonBounds = await signIn.boundingBox();
      const iconBounds = await icon.boundingBox();
      expect(buttonBounds).not.toBeNull();
      expect(iconBounds).not.toBeNull();
      if (!buttonBounds || !iconBounds) throw new Error('Missing sign-in icon geometry');
      expect(buttonBounds.width).toBeGreaterThanOrEqual(44);
      expect(buttonBounds.height).toBeGreaterThanOrEqual(44);
      expect(iconBounds.x).toBeGreaterThanOrEqual(buttonBounds.x + 4);
      expect(iconBounds.y).toBeGreaterThanOrEqual(buttonBounds.y + 4);
      expect(iconBounds.x + iconBounds.width).toBeLessThanOrEqual(buttonBounds.x + buttonBounds.width - 4);
      expect(iconBounds.y + iconBounds.height).toBeLessThanOrEqual(buttonBounds.y + buttonBounds.height - 4);
      await page.locator('.rail-expand').click();
      const navigation = page.locator('.navigation-drawer');
      await expect(navigation).toBeVisible();
      const expanded = navigation.locator('.sidebar-account a[href="/signin"]');
      await expect(expanded).toHaveAccessibleName(signInLabel);
      await expect(expanded.locator('.sidebar-auth-label')).toHaveText(signInLabel);
      await expect(expanded.locator('.sidebar-auth-label')).toBeVisible();
      await expect(expanded.locator('.sidebar-auth-icon')).toBeHidden();
      await page.keyboard.press('Escape');
      await expect(navigation).not.toBeVisible();
    }
''';s=s.replace(anchor,extra+anchor);changes[f]=s
patch='';files=[]
for f,target in changes.items():
 relative=f.relative_to(root).as_posix();before=f.read_text();patch+=''.join(difflib.unified_diff(before.splitlines(True),target.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative));(out/(f.name+'.auth-rail-proposed')).write_text(target);files.append({'path':relative,'before_sha256':hashlib.sha256(before.encode()).hexdigest(),'proposed_sha256':hashlib.sha256(target.encode()).hexdigest()})
(out/'auth-rail-layout.patch').write_text(patch);(out/'auth-rail-fix-proposal.json').write_text(json.dumps({'owner':'/root/rendering_audit','status':'Prepared only; live source unchanged','actual_finding':'Collapsed Bengali phone sign-in text spills across rounded button boundary in actual fda captures','changed_files':files,'production_scope':'Website AccountChip and rail CSS only; existing users/x icons, translated accessible names/title, full labels in expanded navigation. Both sign-in and sign-out use same safe icon-only rail treatment. No wording, catalog, font or client code changes.','e2e_scope':'Existing actual Bengali phone home test verifies accessible name, visible contained icon with44px target, hidden rail label, and full label restored in opened navigation drawer; closes drawer before remaining screenshots.','pending':'Apply only after frozen native/browser producer cohort ends; run formatting/typecheck and actual three-browser phone regression, then recapture and visually inspect changed sidebar.'},indent=2));print('Prepared ignored website-only three-file patch')
