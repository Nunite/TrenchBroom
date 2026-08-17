const escapeShortcutText = (value) => String(value)
  .replaceAll("&", "&amp;")
  .replaceAll("<", "&lt;")
  .replaceAll(">", "&gt;")
  .replaceAll('"', "&quot;")
  .replaceAll("'", "&#39;");

const key_str = (key) => {
  if (!keys[key]) {
    console.error("Unknown key", key);
    return "";
  }
  return `<span class="shortcut">${escapeShortcutText(keys[key])}</span>`;
};

const fix_ellipsis = (path) => path.replace("…", "...");

const shortcut_str = (shortcuts) => {
  if (!shortcuts || shortcuts.length === 0 || shortcuts[0].key === "") {
    return "";
  }

  const shortcut = shortcuts[0];
  return [...shortcut.modifiers.map(key_str), key_str(shortcut.key)].join("");
};

const menu_item_str = (rawKey) => {
  const key = fix_ellipsis(rawKey);
  const item = menu[key];
  if (!item) {
    console.error("Unknown menu item", key);
    return `<strong>${escapeShortcutText(key)}</strong>`;
  }

  const path = item.path.map(escapeShortcutText).join(" &raquo; ");
  const shortcut = shortcut_str(item.shortcut);
  return `<strong>${path}${shortcut ? ` (${shortcut})` : ""}</strong>`;
};

const action_str = (rawKey) => {
  const key = fix_ellipsis(rawKey);
  if (!actions[key]) {
    console.error("Unknown action", key);
    return "";
  }
  return `<strong>${shortcut_str(actions[key])}</strong>`;
};

const print_key = (key) => document.write(key_str(key));
const print_menu_item = (key) => document.write(menu_item_str(key));
const print_action = (key) => document.write(action_str(key));

(() => {
  const normalize = (value) => value.toLocaleLowerCase().replace(/\s+/g, " ").trim();

  const sectionText = (heading) => {
    const depth = Number(heading.tagName.slice(1));
    const parts = [];
    let current = heading.nextElementSibling;
    while (current) {
      if (/^H[1-6]$/.test(current.tagName) && Number(current.tagName.slice(1)) <= depth) {
        break;
      }
      parts.push(current.textContent || "");
      current = current.nextElementSibling;
    }
    return normalize(parts.join(" "));
  };

  window.addEventListener("DOMContentLoaded", () => {
    const body = document.body;
    const article = document.getElementById("manual-article");
    const searchInput = document.getElementById("manual-search-input");
    const searchResults = document.getElementById("manual-search-results");
    const navigation = document.getElementById("manual-navigation");
    const navToggle = document.querySelector(".nav-toggle");
    const navScrim = document.querySelector(".navigation-scrim");
    const themeToggle = document.querySelector(".theme-toggle");
    const pageNavigation = document.getElementById("page-navigation-links");
    const currentSectionLabel = document.getElementById("current-section-label");
    const headings = [...article.querySelectorAll("h1[id], h2[id], h3[id]")];
    const navLinks = [...navigation.querySelectorAll("a[href^='#']")];
    const searchIndex = headings.map((heading) => ({
      id: heading.id,
      title: heading.textContent.trim(),
      text: sectionText(heading),
    }));
    let activeResult = -1;

    const closeSearch = () => {
      searchResults.hidden = true;
      searchInput.setAttribute("aria-expanded", "false");
      activeResult = -1;
    };

    const selectResult = (index) => {
      const resultButtons = [...searchResults.querySelectorAll(".search-result")];
      if (resultButtons.length === 0) {
        activeResult = -1;
        return;
      }
      activeResult = (index + resultButtons.length) % resultButtons.length;
      resultButtons.forEach((button, buttonIndex) => {
        button.setAttribute("aria-selected", String(buttonIndex === activeResult));
      });
      resultButtons[activeResult].scrollIntoView({ block: "nearest" });
    };

    const openSection = (id) => {
      closeSearch();
      searchInput.blur();
      history.pushState(null, "", `#${id}`);
      const heading = document.getElementById(id);
      heading.scrollIntoView({ block: "start" });
      heading.focus({ preventScroll: true });
    };

    const renderSearchResults = () => {
      const query = normalize(searchInput.value);
      searchResults.replaceChildren();
      activeResult = -1;

      if (!query) {
        closeSearch();
        return;
      }

      const terms = query.split(" ");
      const matches = searchIndex
        .filter((entry) => terms.every((term) => normalize(`${entry.title} ${entry.text}`).includes(term)))
        .slice(0, 20);

      if (matches.length === 0) {
        const message = document.createElement("p");
        message.className = "search-message";
        message.textContent = body.dataset.searchNoResults;
        searchResults.append(message);
      } else {
        matches.forEach((entry) => {
          const button = document.createElement("button");
          const title = document.createElement("strong");
          const context = document.createElement("span");
          button.type = "button";
          button.className = "search-result";
          button.setAttribute("role", "option");
          button.setAttribute("aria-selected", "false");
          title.textContent = entry.title;
          context.textContent = entry.text.slice(0, 150) || body.dataset.searchEmpty;
          button.append(title, context);
          button.addEventListener("click", () => openSection(entry.id));
          searchResults.append(button);
        });
      }

      searchResults.hidden = false;
      searchInput.setAttribute("aria-expanded", "true");
    };

    searchInput.addEventListener("input", renderSearchResults);
    searchInput.addEventListener("keydown", (event) => {
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        const delta = event.key === "ArrowDown" ? 1 : -1;
        selectResult(activeResult < 0 ? (delta > 0 ? 0 : -1) : activeResult + delta);
      } else if (event.key === "Enter") {
        const resultButtons = searchResults.querySelectorAll(".search-result");
        if (resultButtons.length > 0) {
          event.preventDefault();
          resultButtons[activeResult >= 0 ? activeResult : 0].click();
        }
      } else if (event.key === "Escape") {
        closeSearch();
        searchInput.blur();
      }
    });

    document.addEventListener("keydown", (event) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLocaleLowerCase() === "k") {
        event.preventDefault();
        searchInput.focus();
        searchInput.select();
      } else if (event.key === "Escape") {
        closeSearch();
      }
    });

    document.addEventListener("click", (event) => {
      if (!event.target.closest(".manual-search-wrap")) {
        closeSearch();
      }
    });

    const setNavigationOpen = (open) => {
      body.classList.toggle("navigation-open", open);
      navToggle.setAttribute("aria-expanded", String(open));
      navScrim.hidden = !open;
    };
    navToggle.addEventListener("click", () => setNavigationOpen(!body.classList.contains("navigation-open")));
    navScrim.addEventListener("click", () => setNavigationOpen(false));
    navLinks.forEach((link) => link.addEventListener("click", () => setNavigationOpen(false)));

    const storedTheme = localStorage.getItem("tb-manual-theme");
    if (storedTheme === "light" || storedTheme === "dark") {
      document.documentElement.dataset.theme = storedTheme;
    }
    themeToggle.addEventListener("click", () => {
      const effectiveDark = document.documentElement.dataset.theme === "dark"
        || (!document.documentElement.dataset.theme && matchMedia("(prefers-color-scheme: dark)").matches);
      const nextTheme = effectiveDark ? "light" : "dark";
      document.documentElement.dataset.theme = nextTheme;
      localStorage.setItem("tb-manual-theme", nextTheme);
    });

    const updateAlternateLanguageLinks = () => {
      document.querySelectorAll(".language-switcher a:not([aria-current])").forEach((link) => {
        const baseHref = link.getAttribute("href").split("#")[0];
        link.href = `${baseHref}${location.hash}`;
      });
    };
    window.addEventListener("hashchange", updateAlternateLanguageLinks);
    updateAlternateLanguageLinks();

    const topLevelFor = (heading) => {
      let current = heading;
      while (current && current.tagName !== "H1") {
        current = current.previousElementSibling;
      }
      return current;
    };

    const renderPageNavigation = (topLevelHeading) => {
      pageNavigation.replaceChildren();
      if (!topLevelHeading) {
        return;
      }
      currentSectionLabel.textContent = topLevelHeading.textContent.trim();
      let current = topLevelHeading.nextElementSibling;
      while (current && current.tagName !== "H1") {
        if ((current.tagName === "H2" || current.tagName === "H3") && current.id) {
          const link = document.createElement("a");
          link.href = `#${current.id}`;
          link.textContent = current.textContent.trim();
          link.className = current.tagName === "H3" ? "depth-3" : "depth-2";
          pageNavigation.append(link);
        }
        current = current.nextElementSibling;
      }
    };

    const setActiveHeading = (heading) => {
      navLinks.forEach((link) => link.classList.toggle("active", link.hash === `#${heading.id}`));
      const topLevel = topLevelFor(heading);
      if (pageNavigation.dataset.section !== topLevel?.id) {
        pageNavigation.dataset.section = topLevel?.id || "";
        renderPageNavigation(topLevel);
      }
      [...pageNavigation.querySelectorAll("a")].forEach((link) => {
        link.classList.toggle("active", link.hash === `#${heading.id}`);
      });
    };

    headings.forEach((heading) => heading.tabIndex = -1);
    const headerHeight = parseInt(getComputedStyle(document.documentElement).getPropertyValue("--header-height"), 10);
    const observer = new IntersectionObserver((entries) => {
      const visible = entries.filter((entry) => entry.isIntersecting)
        .sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top);
      if (visible.length > 0) {
        setActiveHeading(visible[0].target);
      }
    }, { rootMargin: `-${headerHeight + 8}px 0px -72% 0px` });
    headings.forEach((heading) => observer.observe(heading));

    const initialHeading = location.hash ? document.getElementById(location.hash.slice(1)) : headings[0];
    if (initialHeading) {
      setActiveHeading(initialHeading);
    }
  });
})();
