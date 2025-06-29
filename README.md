# Brave Core

Brave Core is a set of changes, APIs, and scripts used for customizing Chromium to make the Brave browser. Please also check https://github.com/brave/brave-browser

Follow [@brave](https://twitter.com/brave) on Twitter for important announcements.

## Resources

- [Issues](https://github.com/brave/brave-browser/issues)
- [Releases](https://github.com/brave/brave-browser/releases)
- [Documentation and guides](https://github.com/brave/brave-core/blob/master/docs/README.md)
- [Wiki](https://github.com/brave/brave-browser/wiki)

## Community

You can ask questions and interact with the community in the following
locations:
- [Brave Community](https://community.brave.com/)
- [`community`](https://bravesoftware.slack.com) channel on Brave Software's Slack



$ git checkout -b cr121 origin/master  # branch name is cr+Major number

# package.json is saved
$ git add package.json

# From is the tag that is on origin/master, while to is the version being
# committed now in `package.json`
$ git commit -m "Update from Chromium 119.7049.17 to Chromium 120.0.7050.40."
