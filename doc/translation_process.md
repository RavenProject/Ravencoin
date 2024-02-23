Translations
============

The Yottaflux project has been designed to support multiple localisations. This makes adding new phrases, and completely new languages easily achievable. For managing all application translations, Yottaflux makes use of the Transifex online translation management tool.

### Helping to translate (using Transifex)
Currently updating strings in Transifex requires a manual upload of the updated src/qt/locale/yottaflux_en.ts.
This can easily be automated in the future.

Multiple language support is critical in assisting Yottafluxs global adoption, and growth. One of Yottafluxs greatest strengths is cross-border money transfers, any help making that easier is greatly appreciated.

See the [Transifex Yottaflux project](https://www.transifex.com/yottaflux) to assist in translations. You can also join the #translations in [Yottaflux Discord](https://discord.gg/jn6uhur).

### Writing code with translations
We use automated scripts to help extract translations in both Qt, and non-Qt source files. It is rarely necessary to manually edit the files in `src/qt/locale/`. The translation source files must adhere to the following format:
`yottaflux_xx_YY.ts or yottaflux_xx.ts`

`src/qt/locale/yottaflux_en.ts` is treated in a special way. It is used as the source for all other translations. Whenever a string in the source code is changed, this file must be updated to reflect those changes. A custom script is used to extract strings from the non-Qt parts. This script makes use of `gettext`, so make sure that utility is installed (ie, `apt-get install gettext` on Ubuntu/Debian). Once this has been updated, `lupdate` (included in the Qt SDK) is used to update `yottaflux_en.ts`.

To automatically regenerate the `yottaflux_en.ts` file, run the following commands:
```sh
cd src/
make translate
```

`src/qt/yottaflux_locale.qrc` takes care of generating `.qm` (binary compiled) files from `.ts` (source files) files. It’s mostly automated, and you shouldn’t need to worry about it.

**Example Qt translation**
```cpp
QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
```

### Creating a pull-request
For general PRs, you shouldn’t include any updates to the translation source files. They will be updated periodically, primarily around pre-releases, allowing time for any new phrases to be translated before public releases. This is also important in avoiding translation related merge conflicts.

When an updated source file is uploaded to Transifex the new strings will show up as "Remaining" in the Transifex web interface and are ready for translators.


### Creating a Transifex account
Visit the [Transifex Signup](https://www.transifex.com/signup/) page to create an account. Take note of your username and password, as they will be required to configure the command-line tool.

You can find the Yottaflux translation project at [https://www.transifex.com/yottaflux](https://www.transifex.com/yottaflux).

### Installing the Transifex client command-line tool
The client it used to fetch updated translations. If you are having problems, or need more details, see [http://docs.transifex.com/developer/client/setup](http://docs.transifex.com/developer/client/setup)

**For Linux and Mac**

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

**For Windows**

Please see [http://docs.transifex.com/developer/client/setup#windows](http://docs.transifex.com/developer/client/setup#windows) for details on installation.

The Transifex Yottaflux project config file is included as part of the repo. It can be found at `.tx/config`, however you shouldn’t need change anything.

### Synchronising translations
To assist in updating translations, we have created a script to help.

1. `python contrib/devtools/update-translations.py`
2. Update `src/qt/yottaflux_locale.qrc` manually or via
   `ls src/qt/locale/*ts|xargs -n1 basename|sed 's/\(yottaflux_\(.*\)\).ts/<file alias="\2">locale\/\1.qm<\/file>/'`
3. Update `src/Makefile.qt.include` manually or via
   `ls src/qt/locale/*ts|xargs -n1 basename|sed 's/\(yottaflux_\(.*\)\).ts/  qt\/locale\/\1.ts \\/'`
4. `git add` new translations from `src/qt/locale/`

**Do not directly download translations** one by one from the Transifex website, as we do a few post-processing steps before committing the translations.

### Handling Plurals (in source files)
When new plurals are added to the source file, it's important to do the following steps:

1. Open `yottaflux_en.ts` in Qt Linguist (included in the Qt SDK)
2. Search for `%n`, which will take you to the parts in the translation that use plurals
3. Look for empty `English Translation (Singular)` and `English Translation (Plural)` fields
4. Add the appropriate strings for the singular and plural form of the base string
5. Mark the item as done (via the green arrow symbol in the toolbar)
6. Repeat from step 2, until all singular and plural forms are in the source file
7. Save the source file

### Translating a new language
To create a new language template, you will need to edit the languages manifest file `src/qt/yottaflux_locale.qrc` and add a new entry. Below is an example of the English language entry.

```xml
<qresource prefix="/translations">
    <file alias="en">locale/yottaflux_en.qm</filer
    ...
</qresource>
```

**Note:** that the language translation file **must end in `.qm`** (the compiled extension), and not `.ts`.

This process can be automated by a [script](https://github.com/fdoving/yottaflux-maintainer-tools/blob/master/update-translations.py) in [yottaflux-maintainer-tools](https://github.com/fdoving/yottaflux-maintainer-tools/).

### Questions and general assistance
The Yottaflux translation maintainers include *fdov and pocal*. You can find them, and others, in #translations in [Yottaflux Discord](https://discord.gg/jn6uhur).

Announcements will be posten in Discord and on the transifex.com [announcements page](https://www.transifex.com/yottaflux/qt-translation/announcements/).
