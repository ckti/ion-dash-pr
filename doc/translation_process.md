- [Translations](#translations)
  - [Helping to translate (using Transifex)](#helping-to-translate-using-transifex)
  - [Writing code with translations](#writing-code-with-translations)
    - [Example Qt translation](#example-qt-translation)
  - [Creating a pull-request](#creating-a-pull-request)
  - [Creating a Transifex account](#creating-a-transifex-account)
  - [Translator project page](#translator-project-page)
  - [Translator Editor and Instructions URL](#translator-editor-and-instructions-url)
  - [Installing the Transifex client command-line tool](#installing-the-transifex-client-command-line-tool)
    - [For Ubuntu bionic (or later) users](#for-ubuntu-bionic-or-later-users)
    - [For older Linux and Mac](#for-older-linux-and-mac)
    - [For Windows](#for-windows)
  - [Synchronizing translations](#synchronizing-translations)
  - [Handling Plurals (in source files)](#handling-plurals-in-source-files)
  - [Translating a new language](#translating-a-new-language)
  - [Questions and general assistance](#questions-and-general-assistance)

# Translations

The Ion Core project has been designed to support multiple localizations. This makes adding new phrases, and completely new languages easily achievable. For managing all application translations, Ion Core makes use of the Transifex online translation management tool.

## Helping to translate (using Transifex)

Transifex is setup to monitor the GitHub repo for updates, and when code containing new translations is found, Transifex will process any changes. It may take several hours after a pull-request has been merged, to appear in the Transifex web interface.

Multiple language support is critical in assisting Ion’s global adoption, and growth. One of Ion’s greatest strengths is cross-border money transfers, any help making that easier is greatly appreciated.

See the [Transifex Ion project](https://www.transifex.com/ioncoincore/ioncore/) to assist in translations. You should also join the translation mailing list for announcements - see details below.

## Writing code with translations

We use automated scripts to help extract translations in both Qt, and non-Qt source files. It is rarely necessary to manually edit the files in `src/qt/locale/`. The translation source files must adhere to the following format:
`ion_xx_YY.ts or ion_xx.ts`

`src/qt/locale/ion_en.ts` is treated in a special way. It is used as the source for all other translations. Whenever a string in the source code is changed, this file must be updated to reflect those changes. A custom script is used to extract strings from the non-Qt parts. This script makes use of `gettext`, so make sure that utility is installed (ie, `apt-get install gettext` on Ubuntu/Debian). Once this has been updated, `lupdate` (included in the Qt SDK) is used to update `ion_en.ts`.

To automatically regenerate the `ion_en.ts` file, run the following commands:

```bash
cd src/
make translate
```

`contrib/ion-qt.pro` takes care of generating `.qm` (binary compiled) files from `.ts` (source files) files. It’s mostly automated, and you shouldn’t need to worry about it.

### Example Qt translation

```cpp
QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
```

## Creating a pull-request

For general PRs, you shouldn’t include any updates to the translation source files. They will be updated periodically, primarily around pre-releases, allowing time for any new phrases to be translated before public releases. This is also important in avoiding translation related merge conflicts.

When an updated source file is merged into the GitHub repo, Transifex will automatically detect it (although it can take several hours). Once processed, the new strings will show up as "Remaining" in the Transifex web interface and are ready for translators.

To create the pull-request, use the following commands:

```bash
git add src/qt/ionstrings.cpp src/qt/locale/ion_en.ts
git commit
```

## Creating a Transifex account

Visit the [Transifex Signup](https://www.transifex.com/signup/) page to create an account. Take note of your username and password, as they will be required to configure the command-line tool.

## Translator project page

You can find the Ion translation project at [https://www.transifex.com/ioncoincore/ioncore/](https://www.transifex.com/ioncoincore/ioncore/).

## Translator Editor and Instructions URL

Online translation editor: https://www.ioncore.xyz/translate

## Installing the Transifex client command-line tool

The client it used to fetch updated translations. If you are having problems, or need more details, see [http://docs.transifex.com/developer/client/setup](http://docs.transifex.com/developer/client/setup)

### For Ubuntu bionic (or later) users

Upgrade pip with:         `sudo -H pip3 install --upgrade pip`
Install transifex client: `sudo -H pip3 install transifex-client`

Setup your transifex client config with `tx config` or create manually like shown below.

### For older Linux and Mac

`pip install transifex-client`

Setup your transifex client config as follows. Please *ignore the token field*.

```ini
nano ~/.transifexrc

[https://www.transifex.com]
hostname = https://www.transifex.com
password = PASSWORD
token =
username = USERNAME
```

### For Windows

Please see [http://docs.transifex.com/developer/client/setup#windows](http://docs.transifex.com/developer/client/setup#windows) for details on installation.

The Transifex Ion project config file is included as part of the repo. It can be found at `.tx/config`, however you shouldn’t need change anything.

## Synchronizing translations

To assist in updating translations, we have created a script to help.

1. `python contrib/devtools/update-translations.py`
2. Update `src/qt/ion_locale.qrc` manually or via
   `ls src/qt/locale/*ts|xargs -n1 basename|sed 's/\(ion_\(.*\)\).ts/<file alias="\2">locale\/\1.qm<\/file>/'`
3. Update `src/Makefile.qt.include` manually or via
   `ls src/qt/locale/*ts|xargs -n1 basename|sed 's/\(ion_\(.*\)\).ts/  qt\/locale\/\1.ts \\/'`
4. `git add` new translations from `src/qt/locale/`

**Do not directly download translations** one by one from the Transifex website, as we do a few post-processing steps before committing the translations.

## Handling Plurals (in source files)

When new plurals are added to the source file, it's important to do the following steps:

1. Open `ion_en.ts` in Qt Linguist (included in the Qt SDK)
2. Search for `%n`, which will take you to the parts in the translation that use plurals
3. Look for empty `English Translation (Singular)` and `English Translation (Plural)` fields
4. Add the appropriate strings for the singular and plural form of the base string
5. Mark the item as done (via the green arrow symbol in the toolbar)
6. Repeat from step 2, until all singular and plural forms are in the source file
7. Save the source file

## Translating a new language

To create a new language template, you will need to edit the languages manifest file `src/qt/ion_locale.qrc` and add a new entry. Below is an example of the English language entry.

```xml
<qresource prefix="/translations">
    <file alias="en">locale/ion_en.qm</file>
    ...
</qresource>
```

**Note:** that the language translation file **must end in `.qm`** (the compiled extension), and not `.ts`.

## Questions and general assistance

You can find translation maintainers, and others, in the [ION Discord](https://discord.gg/vuZn7gC).

Announcements will be posted during application pre-releases to notify translators to check for updates.
