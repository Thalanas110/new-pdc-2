import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';

const root = process.cwd();

function readProjectFile(path: string) {
  return readFileSync(join(root, path), 'utf8');
}

describe('TanStack Start template integration', () => {
  it('uses TanStack Start while keeping the dev server on port 5173', () => {
    const packageJson = JSON.parse(readProjectFile('package.json')) as {
      scripts: Record<string, string>;
      dependencies: Record<string, string>;
      devDependencies: Record<string, string>;
    };
    const viteConfig = readProjectFile('vite.config.ts');
    const nginxConfig = readProjectFile('docker/nginx.conf');

    expect(packageJson.dependencies['@tanstack/react-start']).toBeDefined();
    expect(packageJson.devDependencies['@tanstack/router-cli']).toBeDefined();
    expect(packageJson.scripts.dev).toContain('--port 5173');
    expect(packageJson.scripts.dev).not.toContain('3000');
    expect(viteConfig).toContain('tanstackStart');
    expect(viteConfig).toContain('spa:');
    expect(viteConfig).toContain('enabled: true');
    expect(viteConfig).toContain('port: 5173');
    expect(nginxConfig).toContain('/_shell.html');
  });

  it('adopts the template file-routing entrypoints', () => {
    expect(existsSync(join(root, 'src/routeTree.gen.ts'))).toBe(true);
    expect(readProjectFile('src/router.tsx')).toContain('export const getRouter');
    expect(readProjectFile('src/routes/__root.tsx')).toContain('createRootRouteWithContext');
    expect(readProjectFile('src/routes/_public/index.tsx')).toContain("createFileRoute('/_public/')");
  });
});
