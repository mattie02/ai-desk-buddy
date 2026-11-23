# Maintaining the docs

The Markdown files in `docs/wiki/` are the canonical documentation. Update them in the main repository alongside code changes. The GitHub wiki is a rendered copy; avoid editing it separately.

## Check and render

From the project root:

```sh
python3 scripts/publish_wiki.py --check
python3 scripts/publish_wiki.py --output /tmp/ai-desk-buddy-wiki-preview
```

The script checks local Markdown links and code fences, then renders repository-style `.md` page links as GitHub wiki page links. It does not read local credentials or publish automatically.

## Publish

The repository must have Wikis enabled and an initial page created through GitHub. Clone the separate wiki repository into a new directory, render the source pages there, review, commit, and push:

```sh
git clone https://github.com/mattie02/ai-desk-buddy.wiki.git /tmp/ai-desk-buddy-wiki
python3 scripts/publish_wiki.py --output /tmp/ai-desk-buddy-wiki
git -C /tmp/ai-desk-buddy-wiki diff --check
git -C /tmp/ai-desk-buddy-wiki add '*.md'
git -C /tmp/ai-desk-buddy-wiki commit -m "Update project documentation"
git -C /tmp/ai-desk-buddy-wiki push
```

Use a fresh directory or an existing clean wiki clone. Rendering overwrites matching page filenames but does not remove other pages. Review obsolete pages explicitly if the documentation is reorganized.

Record what was actually tested, including hardware observations and limitations. Do not turn earlier bench evidence into a claim that every later revision was retested. Keep credentials, audio recordings, and private configuration out of documentation and Git history.
