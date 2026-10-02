package me.magnum.melonds.ui.settings.fragments

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.selection.selectable
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.res.colorResource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.MaterialTheme
import androidx.compose.material.RadioButton
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import androidx.fragment.app.Fragment
import androidx.preference.PreferenceManager
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.input.SoftInputBehaviour
import me.magnum.melonds.ui.common.MelonPreviewSet
import me.magnum.melonds.ui.common.component.text.CaptionText
import me.magnum.melonds.ui.settings.PreferenceFragmentTitleProvider
import me.magnum.melonds.ui.settings.SettingsTheme

class SoftInputBehaviourPreferencesFragment : Fragment(), PreferenceFragmentTitleProvider {

    override fun getTitle() = getString(R.string.soft_input_behaviour)

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                SettingsTheme {
                    SoftInputBehaviourPreferencesScreen()
                }
            }
        }
    }
}

@Composable
private fun SoftInputBehaviourPreferencesScreen() {
    val context = LocalContext.current
    val resources = LocalResources.current
    val behaviourValues = remember {
        resources.getStringArray(R.array.soft_input_behaviour)
    }
    val behaviourOptions = remember {
        resources.getStringArray(R.array.soft_input_behaviour_options)
    }
    val behaviourDescriptions = remember {
        resources.getStringArray(R.array.soft_input_behaviour_descriptions)
    }
    val sharedPreferences = remember {
        PreferenceManager.getDefaultSharedPreferences(context)
    }
    var softInputBehaviour by remember(sharedPreferences) {
        val initialPreference = sharedPreferences.getString("soft_input_behaviour", "hide_system_buttons_when_controller_connected")
        val initialBehaviour = SoftInputBehaviour.entries[behaviourValues.indexOf(initialPreference)]
        mutableStateOf(initialBehaviour)
    }

    Column(
        modifier = Modifier.fillMaxSize()
            .verticalScroll(rememberScrollState())
            .selectableGroup()
            .safeDrawingPadding(),
    ) {
        SoftInputBehaviour.entries.forEachIndexed { index, behaviour ->
            SoftInputBehaviourEntry(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 6.dp),
                title = behaviourOptions[index],
                description = behaviourDescriptions[index],
                selected = softInputBehaviour == behaviour,
                onClick = {
                    softInputBehaviour = behaviour
                    sharedPreferences.edit {
                        putString("soft_input_behaviour", behaviourValues[index])
                    }
                }
            )
        }
    }
}

@Composable
private fun SoftInputBehaviourEntry(
    modifier: Modifier = Modifier,
    title: String,
    description: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    Row(
        modifier = modifier
            .onFocusChanged { focused = it.isFocused }
            .clip(RoundedCornerShape(18.dp))
            .background(colorResource(if (selected) R.color.settings_focus else R.color.settings_surface))
            .border(
                width = if (selected || focused) 2.dp else 1.dp,
                color = colorResource(if (selected || focused) R.color.settings_accent else R.color.settings_border),
                shape = RoundedCornerShape(18.dp),
            )
            .selectable(selected = selected, role = Role.RadioButton, onClick = onClick)
            .padding(16.dp),
        horizontalArrangement = Arrangement.spacedBy(32.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(
            selected = selected,
            onClick = null,
        )
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.body1,
            )
            CaptionText(
                text = description,
                style = MaterialTheme.typography.body2,
            )
        }
    }
}

@MelonPreviewSet
@Composable
private fun PreviewSoftInputBehaviourPreferencesScreen() {
    SettingsTheme {
        SoftInputBehaviourPreferencesScreen()
    }
}