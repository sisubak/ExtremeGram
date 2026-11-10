# ExtremeGram — рабочий контекст и TODO

> Файл для сохранения контекста между сессиями/компактами.
> Читать вместе с AGENTS.md. Обновлять после каждой завершённой задачи.

## Идея проекта
ExtremeGram — форк tdesktop (Telegram Desktop) с расширенными инструментами
модерации и кастомизацией интерфейса. Ветка: `feature/extremegram-redesign`.
Репозиторий: `sisubak/ExtremeGram` (origin). Основная работа ведётся в
`Telegram/SourceFiles/custom/` + точечные правки upstream-файлов.

## Ключевые фичи форка
- Bot Mode: команды в чате `!mute`, `!ban`, `!stats`, `!help`, `!stop`, `!whois`
  (bot_mode_manager.{h,cpp}). `!whois` = ID + приблизительная дата регистрации
  по ID (kIdBirthMarks, якорь 1.766B ≈ 15.01.2026, экстраполяция для новых ID)
  + точная дата входа в группу через channels.GetParticipant.
- EnhancedMuteDialog (extremegram_mute_box.{h,cpp}): парсинг срока из текста
  (сек/мин/час/день/нед/год), причина, авто-сообщение «Ты был замучен».
  Вход: из меню «Ограничить» (window_peer_menu.cpp ShowSenderRestrictions)
  и из moderate_messages_box.cpp (строка-ссылка «Ограничить через улучшенный
  диалог...»), только если опция ExtremeMuteDialog включена.
- Client Badge Detector (client_badge_detector.{h,cpp}): маркер в аватарке
  (kMarkerR/G/B), badgeWidth()=20, drawBadgeGetWidth() рисует тинтованную
  иконку (padLeft 3 + 14px + 2). Показывается: у имён в сообщениях
  (history_view_message.cpp ~2617), в списке чатов (dialogs/ui/dialogs_layout.cpp
  ~933 — добавлено 53b41f4), в списке участников (info/profile/
  info_profile_members_controllers.cpp MemberListRow::paintNameIconGetWidth —
  добавлено 53b41f4), в профиле (info_profile_top_bar.cpp ~587).
  ПКМ по сообщению → «Сведения о клиенте ExtremeGram» → бокс (имя жирным,
  @username, ID + кнопка «Копировать ID», регистрация, вход в группу,
  поясняющий текст) — history_view_context_menu.cpp AddExtremeBadgeAction.
- Slowmode tracker (slowmode_tracker.cpp): Custom::SlowmodeFor(session) —
  рисует dial-иконку таймера у имён ограниченных юзеров в сообщениях.
- Typing indicator (typing_indicator.{h,cpp}): полоса «печатает…» над полем
  ввода; премиум/NFT статус-эмодзи (Data::EmojiStatusCustomId), аватары,
  акцентная заливка из DesignTokens.
- Redesign (redesign.{h,cpp}): режимы Classic/Modern/Ultra через DesignTokens
  (radiusId, cardAlpha, accentWashAlpha, accent) + TokensChanged()/TokensStream.
  ПОКА меняет только цвета/радиусы/заливки — НЕ layout (см. TODO п.6).
- EnhancedSettings (core/enhanced_settings.{h,cpp}): опции форка. Порядок
  OptionId и дескрипторов позиционный — новые опции добавлять в конец
  (перед Count) и дескриптор в ту же позицию.
- Online spam interval: api_updates.cpp updateOnline — интервал «печатает…»
  (OptionId::OnlineSpamInterval, слайдер в settings_extremegram.cpp
  BuildOnlineSpamInterval).
- Настройки форка: settings/sections/settings_extremegram.cpp (кнопки,
  слайдеры, about-тексты через builder.addDividerText).

## Сборка и CI
- Локально НЕ собрать: сабмодули пустые, нет Qt-заголовков. Проверка только CI.
- CI: workflow_dispatch `extremegram-build.yml`, ветка
  `feature/extremegram-redesign`. Токен: /tmp/opencode/ghtoken (если пропал —
  спросить у пользователя новый PAT). Dispatch:
  curl -X POST -H "Authorization: token $(cat /tmp/opencode/ghtoken)" \
    https://api.github.com/repos/sisubak/ExtremeGram/actions/workflows/extremegram-build.yml/dispatches \
    -d '{"ref":"feature/extremegram-redesign"}'
- Сборка ~1.5–2 ч. Успех → релиз `build-<run_id>` с AppImage + sha256.
- Логи упавшей сборки: jobs → /actions/jobs/<job_id>/logs, grep "error:".
- Пуш иногда падает "Recv failure" — повторить через sleep 40–120.

## Уроки компиляции (дорого добытые)
- ButtonArgs designated initializers: строго порядок объявления полей
  (.id,.title,.st,.icon,.container,.label,.toggled,.onClick,.keywords,...),
  иначе непонятная ошибка brace-init.
- Lang: `{count}` только в plural-ключах (иначе langgen error 806);
  для не-plural — `{amount}` + `lt_amount`; tr::phrase<lngtag_amount>(tr::now)
  возвращает QString, флаги (RichLangValue) НЕ передавать; для producer —
  оборачивать в rpl::single().
- SessionController::uiShow() → shared_ptr<ChatHelpers::Show>;
  GenericBox::uiShow() → shared_ptr<Ui::Show> (showToast/showBox есть).
- MTPDchannelParticipantCreator НЕ имеет vdate (есть у Participant/Admin/
  Banned/Self) — для создателя дату входа не показывать (FormatJoinDateTime(-1)
  → «неизвестно»).
- Новые custom-файлы регистрировать в Telegram/CMakeLists.txt (~606–619).
- `addAction(text, lambda, &st::menuIconInfo)` — валидный порядок аргументов.
- box->addButton(rpl::single(QString), fn) — для произвольного текста.
- Ui::FlatLabel НЕ имеет ctor с TextWithEntities — только
  rpl::producer<TextWithEntities>: оборачивать rpl::single(Ui::Text::Bold/Link(...)).
- Если в lang-строке убрал {amount} — убрать и lt_amount из вызова phrase,
  иначе "no match for call to (const tr::phrase<>)". 

## Текущее состояние (на момент последнего обновления)
- Последний зелёный релиз: build-42741996523 (коммит d9ad843) — у пользователя.
- 133d5f9 (фикс-батч: typing bar, slowmode-стрелка/бустеры, interval,
  mute-link) — сборка 42822671841 идёт.
- 087579f (layout-редизайн: карточки в списке чатов, плотный Ultra-список,
  карточный Modern-профиль, минималистичный Ultra-профиль) — сборка
  42825628094 идёт.
- Токены DesignTokens теперь имеют флаги chatListCards/denseChatList/
  cardProfile/minimalProfile. Высота строки Ultra = 56px (dialogs_row.cpp
  recountHeight); при смене режима InnerWidget пересчитывает высоты через
  Custom::TokensChanged.
- Иконки/бейдж: тинт по поверхности уже передаётся во всех местах
  (список чатов, участники, профиль, сообщения, упоминания) — задача 7
  считается закрытой, проверять визуально.

## TODO (актуальные баги и задачи от пользователя)
1. [x] Typing bar: эмодзи «улетал вбок» — откат 2a15d51 (64b0265), снова
   сразу после аватарок, перед текстом.
2. [x] Иконка таймера slowmode: откат ca8e89d + стрелка против часовой
   (θ = 0.5π + (1-ratio)·2π, arc span положительный).
3. [x] Бустеры и slowmode: эвристика — повторная отправка до истечения своего
   таймера => exempt (base::flat_set _exempt), таймер снимается. Точное
   определение через premium.getBoostsList — на будущее.
4. [x] Бейдж ExtremeGram в списке чатов и списке участников — сделано
   в 53b41f4 (проверить на сборке 42815391261).
5. [x] Online spam interval: значение буквальное, 0 = всегда «в сети»
   (без мигания), максимум 600000 мс (слайдер, constraint, about-текст).
6. [~] Redesign Modern/Ultra: Modern — карточки в списке чатов + карточный
   профиль; Ultra — плотный компактный список + минималистичный профиль.
   В работе.
7. [~] Тинт всех кастомных иконок форка под тему/фон. В работе.
8. [x] «Ограничить через улучшенный диалог...» — причина: FlatLabel ловит
   клики только по link-энтити. Текст обёрнут в Ui::Text::Link.
9. [x] Создан этот файл (AGENT_TODO.md).

## Открытые вопросы
- Нет. Решения: Modern = карточки (список чатов + профиль), Ultra = плотный
  компакт; иконки форка тинтовать под тему/фон.

## Правила работы
- Стиль: tdesktop (CamelCase, auto, u"..."_q, crl::time в anon namespace).
- Без комментариев в коде, без эмодзи, минимальные диффы.
- Не переписывать файлы целиком без необходимости.
- Суб-агентов не использовать (запрет пользователя).
- Пользователь тестирует AppImage сам; живые инстансы не трогать без просьбы.
